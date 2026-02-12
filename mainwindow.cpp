#include "mainwindow.h"
#include "deepcooldevice.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QStatusBar>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QRegularExpression>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , device(nullptr)
    , isConnected(false)
    , isMonitoring(false)
{
    setupUI();

    // Setup update timer
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &MainWindow::updateSystemInfo);

    // Initial device detection
    detectDevices();
}

MainWindow::~MainWindow()
{
    if (device) {
        disconnectFromDevice();
        delete device;
    }
}

void MainWindow::setupUI()
{
    setWindowTitle("DeepCool MYSTIQUE 360 Controller");
    setMinimumSize(600, 500);

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

    // Device Selection Group
    QGroupBox *deviceGroup = new QGroupBox("Device Connection", this);
    QHBoxLayout *deviceLayout = new QHBoxLayout(deviceGroup);

    deviceComboBox = new QComboBox(this);
    refreshButton = new QPushButton("Refresh", this);
    connectButton = new QPushButton("Connect", this);
    disconnectButton = new QPushButton("Disconnect", this);
    disconnectButton->setEnabled(false);

    deviceLayout->addWidget(new QLabel("Device:", this));
    deviceLayout->addWidget(deviceComboBox, 1);
    deviceLayout->addWidget(refreshButton);
    deviceLayout->addWidget(connectButton);
    deviceLayout->addWidget(disconnectButton);

    mainLayout->addWidget(deviceGroup);

    // Monitoring Control Group
    QGroupBox *monitorGroup = new QGroupBox("Monitoring Control", this);
    QHBoxLayout *monitorLayout = new QHBoxLayout(monitorGroup);

    startMonitorButton = new QPushButton("Start Monitoring", this);
    stopMonitorButton = new QPushButton("Stop Monitoring", this);
    startMonitorButton->setEnabled(false);
    stopMonitorButton->setEnabled(false);

    monitorLayout->addWidget(startMonitorButton);
    monitorLayout->addWidget(stopMonitorButton);
    monitorLayout->addStretch();

    mainLayout->addWidget(monitorGroup);

    // System Information Group
    QGroupBox *infoGroup = new QGroupBox("System Information", this);
    QVBoxLayout *infoLayout = new QVBoxLayout(infoGroup);

    cpuTempLabel = new QLabel("CPU Temperature: -- °C", this);
    cpuUsageLabel = new QLabel("CPU Usage: -- %", this);
    gpuTempLabel = new QLabel("GPU Temperature: -- °C", this);
    gpuUsageLabel = new QLabel("GPU Usage: -- %", this);
    ramUsageLabel = new QLabel("RAM Usage: -- %", this);

    QFont labelFont;
    labelFont.setPointSize(11);
    cpuTempLabel->setFont(labelFont);
    cpuUsageLabel->setFont(labelFont);
    gpuTempLabel->setFont(labelFont);
    gpuUsageLabel->setFont(labelFont);
    ramUsageLabel->setFont(labelFont);

    infoLayout->addWidget(cpuTempLabel);
    infoLayout->addWidget(cpuUsageLabel);
    infoLayout->addWidget(gpuTempLabel);
    infoLayout->addWidget(gpuUsageLabel);
    infoLayout->addWidget(ramUsageLabel);

    mainLayout->addWidget(infoGroup);

    // Display Settings Group
    QGroupBox *settingsGroup = new QGroupBox("Display Settings", this);
    QVBoxLayout *settingsLayout = new QVBoxLayout(settingsGroup);

    QHBoxLayout *modeLayout = new QHBoxLayout();
    displayModeCombo = new QComboBox(this);
    displayModeCombo->addItems({"CPU Info", "GPU Info", "System Overview", "Custom"});
    modeLayout->addWidget(new QLabel("Display Mode:", this));
    modeLayout->addWidget(displayModeCombo, 1);
    settingsLayout->addLayout(modeLayout);

    QHBoxLayout *rotationLayout = new QHBoxLayout();
    rotationCombo = new QComboBox(this);
    rotationCombo->addItems({"0°", "90°", "180°", "270°"});
    rotationLayout->addWidget(new QLabel("Screen Rotation:", this));
    rotationLayout->addWidget(rotationCombo, 1);
    settingsLayout->addLayout(rotationLayout);

    QHBoxLayout *intervalLayout = new QHBoxLayout();
    updateIntervalSpinBox = new QSpinBox(this);
    updateIntervalSpinBox->setRange(100, 5000);
    updateIntervalSpinBox->setValue(1000);
    updateIntervalSpinBox->setSuffix(" ms");
    intervalLayout->addWidget(new QLabel("Update Interval:", this));
    intervalLayout->addWidget(updateIntervalSpinBox, 1);
    settingsLayout->addLayout(intervalLayout);

    fahrenheitCheckBox = new QCheckBox("Use Fahrenheit (°F)", this);
    alarmCheckBox = new QCheckBox("Enable Temperature Alarm", this);

    settingsLayout->addWidget(fahrenheitCheckBox);
    settingsLayout->addWidget(alarmCheckBox);

    mainLayout->addWidget(settingsGroup);

    // Connection Status
    connectionStatusLabel = new QLabel("Status: Not Connected", this);
    connectionStatusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    mainLayout->addWidget(connectionStatusLabel);

    mainLayout->addStretch();

    setCentralWidget(centralWidget);
    statusBar()->showMessage("Ready");

    // Connect signals
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::detectDevices);
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectToDevice);
    connect(disconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectFromDevice);
    connect(startMonitorButton, &QPushButton::clicked, this, &MainWindow::startMonitoring);
    connect(stopMonitorButton, &QPushButton::clicked, this, &MainWindow::stopMonitoring);
    connect(displayModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::changeDisplayMode);
    connect(rotationCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::changeRotation);
    connect(updateIntervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::updateRefreshRate);
    connect(fahrenheitCheckBox, &QCheckBox::toggled, this, &MainWindow::toggleFahrenheit);
    connect(alarmCheckBox, &QCheckBox::toggled, this, &MainWindow::toggleAlarm);
}

void MainWindow::detectDevices()
{
    deviceComboBox->clear();
    updateStatusBar("Scanning for DeepCool devices...");

    // Use Device static method to scan for devices
    devices = Device::detectDevices();

    qDebug() << "Found" << devices.size() << "device(s)";

    if (devices.isEmpty()) {
        deviceComboBox->addItem("No DeepCool devices found");
        updateStatusBar("No devices found. Check connection and run as root if needed.");
    } else {
        for (const DeviceInfo &devInfo : devices) {
            // Create display text with device name and type
            QString displayText = devInfo.displayName;
            if (devInfo.type == DEVICE_TYPE_USB_VENDOR) {
                displayText += QString(" (USB Bus %1)").arg(devInfo.busNumber);
            } else if (devInfo.type == DEVICE_TYPE_HID) {
                displayText += " (HID)";
            }

            // Add device with display name, store path as user data
            deviceComboBox->addItem(displayText, devInfo.devicePath);

            qDebug() << "  -" << devInfo.displayName << "at" << devInfo.devicePath;
        }
        updateStatusBar(QString("Found %1 DeepCool device(s)").arg(devices.size()));
    }
}

void MainWindow::connectToDevice()
{
    if (deviceComboBox->currentText().contains("No")) {
        QMessageBox::warning(this, "Connection Error",
                           "No valid device selected. Please refresh and try again.");
        return;
    }

    // // Get the device path stored as user data
    // QString devicePath = deviceComboBox->currentData().toString();


    // Create device instance if not exists
    if (!device) {
        device = new DeepCoolDevice();
    }

    // Get the device stored as user data
    // Try to open the device
    if (!device->openDevice(devices.at(deviceComboBox->currentIndex()))) {
        QMessageBox::critical(this, "Connection Failed",
                            QString("Failed to open device: %1\n\n"
                                    "Make sure you have permissions (run as root or setup udev rules)")
                            .arg(device->getDeviceInfo()));
        return;
    }

    isConnected = true;
    connectButton->setEnabled(false);
    disconnectButton->setEnabled(true);
    deviceComboBox->setEnabled(false);
    startMonitorButton->setEnabled(true);

    connectionStatusLabel->setText(QString("Status: Connected to %1").arg(device->getDeviceName()));
    connectionStatusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");

    updateStatusBar("Connected to device successfully");
    QMessageBox::information(this, "Success",
                           QString("Connected to %1!\n\nClick 'Start Monitoring' to begin sending data to the display.")
                           .arg(device->getDeviceName()));
}

void MainWindow::disconnectFromDevice()
{
    // Stop monitoring first
    if (isMonitoring) {
        stopMonitoring();
    }

    // Close the device
    if (device) {
        device->closeDevice();
    }

    isConnected = false;
    connectButton->setEnabled(true);
    disconnectButton->setEnabled(false);
    deviceComboBox->setEnabled(true);
    startMonitorButton->setEnabled(false);
    stopMonitorButton->setEnabled(false);

    connectionStatusLabel->setText("Status: Not Connected");
    connectionStatusLabel->setStyleSheet("QLabel { color: red; font-weight: bold; }");

    updateStatusBar("Disconnected from device");
}

void MainWindow::startMonitoring()
{
    if (!isConnected || !device) {
        QMessageBox::warning(this, "Cannot Start", "No device connected!");
        return;
    }

    isMonitoring = true;
    startMonitorButton->setEnabled(false);
    stopMonitorButton->setEnabled(true);

    // Start the update timer
    updateTimer->start(updateIntervalSpinBox->value());

    connectionStatusLabel->setText(QString("Status: Connected to %1 - Monitoring Active")
                                   .arg(device->getDeviceName()));
    connectionStatusLabel->setStyleSheet("QLabel { color: blue; font-weight: bold; }");

    updateStatusBar("Monitoring started - sending data to display");
}

void MainWindow::stopMonitoring()
{
    if (!isMonitoring) {
        return;
    }

    isMonitoring = false;
    updateTimer->stop();

    startMonitorButton->setEnabled(true);
    stopMonitorButton->setEnabled(false);

    connectionStatusLabel->setText(QString("Status: Connected to %1")
                                   .arg(device->getDeviceName()));
    connectionStatusLabel->setStyleSheet("QLabel { color: green; font-weight: bold; }");

    updateStatusBar("Monitoring stopped");
}

void MainWindow::updateSystemInfo()
{
    if (!isConnected || !device) return;

    // Get system information
    float cpuTemp = getCPUTemperature();
    float cpuUsage = getCPUUsage();
    float gpuTemp = getGPUTemperature();
    float gpuUsage = getGPUUsage();
    float ramUsage = getRAMUsage();

    // Update labels
    QString tempUnit = fahrenheitCheckBox->isChecked() ? "°F" : "°C";
    cpuTempLabel->setText(QString("CPU Temperature: %1 %2").arg(cpuTemp, 0, 'f', 1).arg(tempUnit));
    cpuUsageLabel->setText(QString("CPU Usage: %1 %").arg(cpuUsage, 0, 'f', 1));
    gpuTempLabel->setText(QString("GPU Temperature: %1 %2").arg(gpuTemp, 0, 'f', 1).arg(tempUnit));
    gpuUsageLabel->setText(QString("GPU Usage: %1 %").arg(gpuUsage, 0, 'f', 1));
    ramUsageLabel->setText(QString("RAM Usage: %1 %").arg(ramUsage, 0, 'f', 1));

    // Send data to DeepCool device display
    SystemData data;
    data.cpuTemp = cpuTemp;
    data.cpuUsage = cpuUsage;
    data.gpuTemp = gpuTemp;
    data.gpuUsage = gpuUsage;
    data.ramUsage = ramUsage;
    data.useFahrenheit = fahrenheitCheckBox->isChecked();

    device->updateDisplay(data);
}

void MainWindow::changeDisplayMode()
{
    if (!isConnected || !device) return;

    // Map combo box index to DisplayMode enum
    DisplayMode mode = static_cast<DisplayMode>(displayModeCombo->currentIndex());

    if (device->setDisplayMode(mode)) {
        updateStatusBar(QString("Display mode changed to: %1").arg(displayModeCombo->currentText()));
    } else {
        updateStatusBar("Failed to change display mode");
    }
}

void MainWindow::changeRotation()
{
    if (!isConnected || !device) return;

    ScreenRotation rotation = static_cast<ScreenRotation>(rotationCombo->currentIndex());

    if (device->setRotation(rotation)) {
        updateStatusBar(QString("Screen rotation set to: %1").arg(rotationCombo->currentText()));
    } else {
        updateStatusBar("Failed to set screen rotation");
    }
}

void MainWindow::updateRefreshRate()
{
    if (updateTimer->isActive()) {
        updateTimer->setInterval(updateIntervalSpinBox->value());
    }
}

void MainWindow::toggleFahrenheit(bool checked)
{
    // Temperature conversion will happen in updateSystemInfo()
    if (isConnected) {
        updateSystemInfo();
    }
}

void MainWindow::toggleAlarm(bool checked)
{
    if (!isConnected || !device) return;

    if (device->setAlarm(checked)) {
        updateStatusBar(QString("Temperature alarm %1").arg(checked ? "enabled" : "disabled"));
    } else {
        updateStatusBar("Failed to change alarm setting");
    }
}

void MainWindow::updateStatusBar(const QString &message)
{
    statusBar()->showMessage(message, 3000);
}

// System monitoring functions (simplified implementations)
float MainWindow::getCPUTemperature()
{
    // Read from /sys/class/thermal/thermal_zone*/temp
    QFile tempFile("/sys/class/thermal/thermal_zone0/temp");
    if (tempFile.open(QIODevice::ReadOnly)) {
        QTextStream in(&tempFile);
        QString temp = in.readAll().trimmed();
        float celsius = temp.toFloat() / 1000.0f;

        if (fahrenheitCheckBox->isChecked()) {
            return celsius * 9.0f / 5.0f + 32.0f;
        }
        return celsius;
    }
    return 0.0f;
}

float MainWindow::getCPUUsage()
{
    // Simplified CPU usage calculation
    // TODO: Implement proper CPU usage monitoring
    static QFile statFile("/proc/stat");
    return 45.5f; // Placeholder
}

float MainWindow::getGPUTemperature()
{
    // TODO: Implement GPU temperature reading
    // This varies by GPU vendor (NVIDIA, AMD, Intel)
    return 55.0f; // Placeholder
}

float MainWindow::getGPUUsage()
{
    // TODO: Implement GPU usage monitoring
    return 30.0f; // Placeholder
}

float MainWindow::getRAMUsage()
{
    QFile meminfoFile("/proc/meminfo");
    if (meminfoFile.open(QIODevice::ReadOnly)) {
        QTextStream in(&meminfoFile);
        QString content = in.readAll();

        // Parse MemTotal and MemAvailable
        qint64 memTotal = 0, memAvailable = 0;
        QStringList lines = content.split('\n');

        QRegularExpression whitespaceRegex("\\s+");

        for (const QString &line : lines) {
            if (line.startsWith("MemTotal:")) {
                memTotal = line.split(whitespaceRegex)[1].toLongLong();
            } else if (line.startsWith("MemAvailable:")) {
                memAvailable = line.split(whitespaceRegex)[1].toLongLong();
            }
        }

        if (memTotal > 0) {
            return ((memTotal - memAvailable) / (float)memTotal) * 100.0f;
        }
    }
    return 0.0f;
}