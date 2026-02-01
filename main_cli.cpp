/**
 * DeepCool Digital - CLI Version
 *
 * A headless command-line application for controlling DeepCool digital
 * cooling devices on Linux servers without a GUI.
 *
 * Usage: deepcool-cli [options]
 *
 * Options:
 *   -l, --list           List available devices and exit
 *   -d, --device <path>  Device path (e.g., /dev/hidraw0) or index (0, 1, ...)
 *   -i, --interval <ms>  Update interval in milliseconds (default: 1000)
 *   -m, --mode <mode>    Display mode: cpu, gpu, system, custom (default: cpu)
 *   -f, --fahrenheit     Use Fahrenheit instead of Celsius
 *   -a, --alarm          Enable temperature alarm
 *   -v, --verbose        Enable verbose output
 *   -D, --daemon         Run as daemon (fork to background)
 *   -h, --help           Show help message
 */

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDir>
#include <QDebug>

#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <vector>

#include "device.h"
#include "deepcooldevice.h"

// Global pointers for signal handler cleanup
static DeepCoolDevice* g_device = nullptr;
static QCoreApplication* g_app = nullptr;
static bool g_verbose = false;
static bool g_running = true;

// Previous CPU stats for usage calculation
static long long prev_idle = 0;
static long long prev_total = 0;

void log(const QString& message) {
    if (g_verbose) {
        QTextStream out(stdout);
        out << "[" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "] "
            << message << Qt::endl;
    }
}

void logError(const QString& message) {
    QTextStream err(stderr);
    err << "[ERROR] " << message << Qt::endl;
}

void logInfo(const QString& message) {
    QTextStream out(stdout);
    out << message << Qt::endl;
}

void signalHandler(int signum) {
    QTextStream out(stdout);
    out << Qt::endl << "Received signal " << signum << ", shutting down..." << Qt::endl;
    g_running = false;

    if (g_device) {
        g_device->close();
    }

    if (g_app) {
        g_app->quit();
    }
}

void setupSignalHandlers() {
    struct sigaction action;
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);
}

bool daemonize() {
    pid_t pid = fork();

    if (pid < 0) {
        logError("Failed to fork");
        return false;
    }

    if (pid > 0) {
        // Parent process - exit
        logInfo(QString("Daemon started with PID %1").arg(pid));
        _exit(0);
    }

    // Child process continues

    // Create new session
    if (setsid() < 0) {
        logError("Failed to create new session");
        return false;
    }

    // Fork again to prevent acquiring a controlling terminal
    pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    // Change working directory
    chdir("/");

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect to /dev/null
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);

    return true;
}

float getCPUTemperature(bool useFahrenheit) {
    float celsius = 0.0f;

    // First, try to find coretemp (Intel) or k10temp (AMD) in hwmon
    // This gives the actual CPU package temperature like 'sensors' command
    QDir hwmonDir("/sys/class/hwmon");
    QStringList hwmons = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& hwmon : hwmons) {
        QString namePath = QString("/sys/class/hwmon/%1/name").arg(hwmon);
        QFile nameFile(namePath);
        if (nameFile.open(QIODevice::ReadOnly)) {
            QString name = QTextStream(&nameFile).readAll().trimmed();
            nameFile.close();

            // Intel CPU: coretemp, AMD CPU: k10temp
            if (name == "coretemp" || name == "k10temp") {
                // Try to find the package/Tctl temperature (usually temp1)
                // Also check for highest core temperature
                float maxTemp = 0.0f;

                // Check temp1 through temp20 (covers most CPUs)
                for (int i = 1; i <= 20; i++) {
                    QString tempPath = QString("/sys/class/hwmon/%1/temp%2_input").arg(hwmon).arg(i);
                    QFile tempFile(tempPath);
                    if (tempFile.open(QIODevice::ReadOnly)) {
                        float temp = QTextStream(&tempFile).readAll().trimmed().toFloat() / 1000.0f;
                        tempFile.close();
                        if (temp > maxTemp && temp < 150) {
                            maxTemp = temp;
                        }
                    }
                }

                if (maxTemp > 0) {
                    celsius = maxTemp;
                    break;
                }
            }
        }
    }

    // Fallback: try thermal zones if hwmon didn't work
    if (celsius <= 0) {
        QStringList thermalPaths = {
            "/sys/class/thermal/thermal_zone0/temp",
            "/sys/class/thermal/thermal_zone1/temp",
            "/sys/class/thermal/thermal_zone2/temp"
        };

        for (const QString& path : thermalPaths) {
            QFile tempFile(path);
            if (tempFile.open(QIODevice::ReadOnly)) {
                QTextStream in(&tempFile);
                QString temp = in.readAll().trimmed();
                float tempCelsius = temp.toFloat() / 1000.0f;
                tempFile.close();

                if (tempCelsius > celsius && tempCelsius < 150) {
                    celsius = tempCelsius;
                }
            }
        }
    }

    if (celsius > 0) {
        if (useFahrenheit) {
            return celsius * 9.0f / 5.0f + 32.0f;
        }
        return celsius;
    }

    return 0.0f;
}

float getCPUUsage() {
    QFile statFile("/proc/stat");
    if (!statFile.open(QIODevice::ReadOnly)) {
        return 0.0f;
    }

    QTextStream in(&statFile);
    QString line = in.readLine();
    statFile.close();

    if (!line.startsWith("cpu ")) {
        return 0.0f;
    }

    // Parse: cpu  user nice system idle iowait irq softirq steal guest guest_nice
    QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() < 5) {
        return 0.0f;
    }

    long long user = parts[1].toLongLong();
    long long nice = parts[2].toLongLong();
    long long system = parts[3].toLongLong();
    long long idle = parts[4].toLongLong();
    long long iowait = parts.size() > 5 ? parts[5].toLongLong() : 0;
    long long irq = parts.size() > 6 ? parts[6].toLongLong() : 0;
    long long softirq = parts.size() > 7 ? parts[7].toLongLong() : 0;
    long long steal = parts.size() > 8 ? parts[8].toLongLong() : 0;

    long long total_idle = idle + iowait;
    long long total = user + nice + system + idle + iowait + irq + softirq + steal;

    float usage = 0.0f;

    if (prev_total > 0) {
        long long diff_idle = total_idle - prev_idle;
        long long diff_total = total - prev_total;

        if (diff_total > 0) {
            usage = (1.0f - (float)diff_idle / (float)diff_total) * 100.0f;
        }
    }

    prev_idle = total_idle;
    prev_total = total;

    return usage;
}

float getGPUTemperature(bool useFahrenheit) {
    float celsius = 0.0f;

    // Try NVIDIA first (nvidia-smi)
    QFile nvidiaSmi("/usr/bin/nvidia-smi");
    if (nvidiaSmi.exists()) {
        FILE* pipe = popen("nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
        if (pipe) {
            char buffer[128];
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                celsius = atof(buffer);
            }
            pclose(pipe);

            if (celsius > 0) {
                if (useFahrenheit) {
                    return celsius * 9.0f / 5.0f + 32.0f;
                }
                return celsius;
            }
        }
    }

    // Try AMD (through hwmon)
    QDir hwmonDir("/sys/class/hwmon");
    QStringList hwmons = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& hwmon : hwmons) {
        QString namePath = QString("/sys/class/hwmon/%1/name").arg(hwmon);
        QFile nameFile(namePath);
        if (nameFile.open(QIODevice::ReadOnly)) {
            QString name = QTextStream(&nameFile).readAll().trimmed();
            nameFile.close();

            if (name == "amdgpu" || name == "radeon") {
                QString tempPath = QString("/sys/class/hwmon/%1/temp1_input").arg(hwmon);
                QFile tempFile(tempPath);
                if (tempFile.open(QIODevice::ReadOnly)) {
                    celsius = QTextStream(&tempFile).readAll().trimmed().toFloat() / 1000.0f;
                    tempFile.close();

                    if (celsius > 0) {
                        if (useFahrenheit) {
                            return celsius * 9.0f / 5.0f + 32.0f;
                        }
                        return celsius;
                    }
                }
            }
        }
    }

    // Try Intel GPU
    QFile intelTemp("/sys/class/drm/card0/device/hwmon/hwmon*/temp1_input");
    // Fallback: no GPU temperature available
    return 0.0f;
}

float getGPUUsage() {
    // Try NVIDIA
    QFile nvidiaSmi("/usr/bin/nvidia-smi");
    if (nvidiaSmi.exists()) {
        FILE* pipe = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
        if (pipe) {
            char buffer[128];
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                float usage = atof(buffer);
                pclose(pipe);
                if (usage >= 0) {
                    return usage;
                }
            }
            pclose(pipe);
        }
    }

    // Try AMD (through /sys)
    QFile amdUsage("/sys/class/drm/card0/device/gpu_busy_percent");
    if (amdUsage.open(QIODevice::ReadOnly)) {
        float usage = QTextStream(&amdUsage).readAll().trimmed().toFloat();
        amdUsage.close();
        return usage;
    }

    return 0.0f;
}

float getRAMUsage() {
    QFile meminfoFile("/proc/meminfo");
    if (!meminfoFile.open(QIODevice::ReadOnly)) {
        return 0.0f;
    }

    QTextStream in(&meminfoFile);
    QString content = in.readAll();
    meminfoFile.close();

    qint64 memTotal = 0, memAvailable = 0;
    QStringList lines = content.split('\n');
    QRegularExpression whitespaceRegex("\\s+");

    for (const QString& line : lines) {
        if (line.startsWith("MemTotal:")) {
            memTotal = line.split(whitespaceRegex)[1].toLongLong();
        } else if (line.startsWith("MemAvailable:")) {
            memAvailable = line.split(whitespaceRegex)[1].toLongLong();
        }
    }

    if (memTotal > 0) {
        return ((memTotal - memAvailable) / (float)memTotal) * 100.0f;
    }

    return 0.0f;
}

void listDevices() {
    logInfo("Scanning for DeepCool devices...\n");

    QVector<DeviceInfo> devices = Device::detectDevices();

    if (devices.isEmpty()) {
        logInfo("No DeepCool devices found.");
        logInfo("\nTroubleshooting:");
        logInfo("  1. Make sure the device is connected via USB");
        logInfo("  2. Run with sudo or set up udev rules");
        logInfo("  3. Check 'lsusb' for vendor ID 3633");
        return;
    }

    logInfo(QString("Found %1 device(s):\n").arg(devices.size()));

    for (int i = 0; i < devices.size(); i++) {
        const DeviceInfo& dev = devices[i];
        QString typeStr = (dev.type == DEVICE_TYPE_HID) ? "HID" : "USB";
        logInfo(QString("  [%1] %2").arg(i).arg(dev.displayName));
        logInfo(QString("      Type: %1").arg(typeStr));
        logInfo(QString("      Path: %1").arg(dev.devicePath));
        logInfo(QString("      VID:PID: %1:%2")
            .arg(dev.vendorId, 4, 16, QChar('0'))
            .arg(dev.productId, 4, 16, QChar('0')));
        if (dev.type == DEVICE_TYPE_USB_VENDOR) {
            logInfo(QString("      Bus: %1, Address: %2").arg(dev.busNumber).arg(dev.deviceAddress));
        }
        logInfo("");
    }
}

DisplayMode parseDisplayMode(const QString& mode) {
    QString m = mode.toLower();
    if (m == "cpu" || m == "0") return MODE_CPU_INFO;
    if (m == "gpu" || m == "1") return MODE_GPU_INFO;
    if (m == "system" || m == "2") return MODE_SYSTEM_OVERVIEW;
    if (m == "custom" || m == "3") return MODE_CUSTOM;
    return MODE_CPU_INFO;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("deepcool-cli");
    QCoreApplication::setApplicationVersion("1.0.0");

    g_app = &app;

    // Command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription("DeepCool Digital Controller - CLI Version\n"
                                     "Control DeepCool digital cooling devices from the command line.");
    parser.addHelpOption();
    parser.addVersionOption();

    // Options
    QCommandLineOption listOption(QStringList() << "l" << "list",
        "List available devices and exit");
    parser.addOption(listOption);

    QCommandLineOption deviceOption(QStringList() << "d" << "device",
        "Device path (/dev/hidrawX) or index (0, 1, ...)", "device", "0");
    parser.addOption(deviceOption);

    QCommandLineOption intervalOption(QStringList() << "i" << "interval",
        "Update interval in milliseconds", "ms", "1000");
    parser.addOption(intervalOption);

    QCommandLineOption modeOption(QStringList() << "m" << "mode",
        "Display mode: cpu, gpu, system, custom", "mode", "cpu");
    parser.addOption(modeOption);

    QCommandLineOption fahrenheitOption(QStringList() << "f" << "fahrenheit",
        "Use Fahrenheit instead of Celsius");
    parser.addOption(fahrenheitOption);

    QCommandLineOption alarmOption(QStringList() << "a" << "alarm",
        "Enable temperature alarm");
    parser.addOption(alarmOption);

    QCommandLineOption verboseOption(QStringList() << "V" << "verbose",
        "Enable verbose output");
    parser.addOption(verboseOption);

    QCommandLineOption daemonOption(QStringList() << "D" << "daemon",
        "Run as daemon (fork to background)");
    parser.addOption(daemonOption);

    parser.process(app);

    g_verbose = parser.isSet(verboseOption);

    // List devices mode
    if (parser.isSet(listOption)) {
        listDevices();
        return 0;
    }

    // Daemon mode
    if (parser.isSet(daemonOption)) {
        if (!daemonize()) {
            logError("Failed to daemonize");
            return 1;
        }
    }

    // Setup signal handlers
    setupSignalHandlers();

    // Check for root permissions
    if (geteuid() != 0) {
        logInfo("Warning: Not running as root. Device access may fail.");
        logInfo("Run with sudo or set up udev rules for non-root access.\n");
    }

    // Detect devices
    log("Detecting devices...");
    QVector<DeviceInfo> devices = Device::detectDevices();

    if (devices.isEmpty()) {
        logError("No DeepCool devices found. Use -l to list devices.");
        return 1;
    }

    // Select device
    DeviceInfo selectedDevice;
    QString deviceArg = parser.value(deviceOption);

    bool isIndex = false;
    int deviceIndex = deviceArg.toInt(&isIndex);

    if (isIndex && deviceIndex >= 0 && deviceIndex < devices.size()) {
        selectedDevice = devices[deviceIndex];
    } else {
        // Try to find by path
        bool found = false;
        for (const DeviceInfo& dev : devices) {
            if (dev.devicePath == deviceArg) {
                selectedDevice = dev;
                found = true;
                break;
            }
        }

        if (!found) {
            // Default to first device
            selectedDevice = devices[0];
            log(QString("Device '%1' not found, using first device").arg(deviceArg));
        }
    }

    logInfo(QString("Selected device: %1").arg(selectedDevice.displayName));
    logInfo(QString("Device path: %1").arg(selectedDevice.devicePath));

    // Create and open device
    DeepCoolDevice device;
    g_device = &device;

    if (!device.open(selectedDevice)) {
        logError(QString("Failed to open device: %1").arg(selectedDevice.devicePath));
        logError("Make sure you have permission to access the device.");
        logError("Try running with sudo or setting up udev rules.");
        return 1;
    }

    logInfo("Device opened successfully.");

    // Parse options
    int interval = parser.value(intervalOption).toInt();
    if (interval < 100) interval = 100;
    if (interval > 10000) interval = 10000;

    bool useFahrenheit = parser.isSet(fahrenheitOption);
    bool enableAlarm = parser.isSet(alarmOption);
    DisplayMode displayMode = parseDisplayMode(parser.value(modeOption));

    logInfo(QString("Update interval: %1 ms").arg(interval));
    logInfo(QString("Display mode: %1").arg(parser.value(modeOption)));
    logInfo(QString("Temperature unit: %1").arg(useFahrenheit ? "Fahrenheit" : "Celsius"));
    logInfo(QString("Alarm: %1").arg(enableAlarm ? "enabled" : "disabled"));
    logInfo("");

    // NOTE: Disabled - Windows software doesn't send separate mode/alarm commands
    // device.setAlarm(enableAlarm);

    // Set display mode (affects what's shown in GHz position)
    device.setDisplayMode(displayMode);

    // Initial CPU usage read (need two samples)
    getCPUUsage();

    logInfo("Starting monitoring... (Press Ctrl+C to stop)\n");

    // Setup update timer
    QTimer updateTimer;
    QObject::connect(&updateTimer, &QTimer::timeout, [&]() {
        if (!g_running) {
            app.quit();
            return;
        }

        // Gather system data
        SystemData data;
        data.cpuTemp = getCPUTemperature(useFahrenheit);
        data.cpuUsage = getCPUUsage();
        data.gpuTemp = getGPUTemperature(useFahrenheit);
        data.gpuUsage = getGPUUsage();
        data.ramUsage = getRAMUsage();
        data.useFahrenheit = useFahrenheit;

        // Send to device
        bool success = device.updateDisplay(data);

        // Log status
        QString tempUnit = useFahrenheit ? "F" : "C";
        log(QString("CPU: %1%2 (%3%) | GPU: %4%5 (%6%) | RAM: %7% | %8")
            .arg(data.cpuTemp, 0, 'f', 1).arg(tempUnit)
            .arg(data.cpuUsage, 0, 'f', 1)
            .arg(data.gpuTemp, 0, 'f', 1).arg(tempUnit)
            .arg(data.gpuUsage, 0, 'f', 1)
            .arg(data.ramUsage, 0, 'f', 1)
            .arg(success ? "OK" : "FAIL"));
    });

    updateTimer.start(interval);

    // Run event loop
    int result = app.exec();

    // Cleanup
    device.close();
    logInfo("\nDevice closed. Goodbye!");

    return result;
}
