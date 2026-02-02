#include "deepcooldevice.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

DeepCoolDevice::DeepCoolDevice()
    : deviceType(DEVICE_TYPE_UNKNOWN)
    , openedViaLegacyMethod(false)
    , usbContext(nullptr)
    , deviceHandle(nullptr)
    , interfaceNumber(0)
    , endpointOut(0x02)
    , endpointIn(0x82)
    , fd(-1)
    , currentMode(MODE_CPU_INFO)
{
    libusb_init(&usbContext);
}

DeepCoolDevice::~DeepCoolDevice()
{
    close();
    if (usbContext) {
        libusb_exit(usbContext);
        usbContext = nullptr;
    }
}

bool DeepCoolDevice::open(const DeviceInfo &devInfo)
{
    if (isOpen()) {
        close();
    }

    deviceInfo = devInfo;
    deviceType = devInfo.type;
    deviceName = devInfo.displayName;
    openedViaLegacyMethod = false;

    if (devInfo.type == DEVICE_TYPE_HID) {
        fd = ::open(devInfo.devicePath.toUtf8().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            return false;
        }
    }
    else if (devInfo.type == DEVICE_TYPE_USB_VENDOR) {
        if (!usbContext) {
            return false;
        }

        libusb_device **devList;
        ssize_t cnt = libusb_get_device_list(usbContext, &devList);
        if (cnt < 0) {
            return false;
        }

        libusb_device *targetDevice = nullptr;
        for (ssize_t i = 0; i < cnt; i++) {
            struct libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(devList[i], &desc) == 0) {
                if (desc.idVendor == devInfo.vendorId && desc.idProduct == devInfo.productId) {
                    targetDevice = devList[i];
                    break;
                }
            }
        }

        if (!targetDevice) {
            libusb_free_device_list(devList, 1);
            return false;
        }

        int ret = libusb_open(targetDevice, &deviceHandle);
        libusb_free_device_list(devList, 1);

        if (ret < 0) {
            return false;
        }

        if (libusb_kernel_driver_active(deviceHandle, interfaceNumber) == 1) {
            libusb_detach_kernel_driver(deviceHandle, interfaceNumber);
        }

        ret = libusb_claim_interface(deviceHandle, interfaceNumber);
        if (ret < 0) {
            close();
            return false;
        }
    }
    else {
        return false;
    }

    if (!verifyDevice()) {
        close();
        return false;
    }

    return true;
}

void DeepCoolDevice::close()
{
    if (deviceType == DEVICE_TYPE_HID && fd >= 0) {
        ::close(fd);
        fd = -1;
    }
    else if (deviceType == DEVICE_TYPE_USB_VENDOR && deviceHandle) {
        libusb_release_interface(deviceHandle, interfaceNumber);
        libusb_close(deviceHandle);
        deviceHandle = nullptr;
    }

    deviceType = DEVICE_TYPE_UNKNOWN;
    openedViaLegacyMethod = false;
}

bool DeepCoolDevice::openDevice(const DeviceInfo &deviceInfo)
{
    return open(deviceInfo);
}

void DeepCoolDevice::closeDevice()
{
    close();
}

bool DeepCoolDevice::verifyDevice()
{
    if (!isOpen()) {
        return false;
    }

    uint16_t vendor = 0;
    uint16_t product = 0;

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            return false;
        }
        vendor = info.vendor;
        product = info.product;
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        vendor = deviceInfo.vendorId;
        product = deviceInfo.productId;
    } else {
        return false;
    }

    if (vendor == DEEPCOOL_VENDOR_ID) {
        switch (product) {
            case 0x0001: deviceName = "DeepCool AK400 DIGITAL"; break;
            case 0x0002: deviceName = "DeepCool AK620 DIGITAL"; break;
            case 0x0003: deviceName = "DeepCool AK500 DIGITAL"; break;
            case 0x0004: deviceName = "DeepCool AK500S DIGITAL"; break;
            case 0x0005: deviceName = "DeepCool CH560 DIGITAL"; break;
            case 0x0006: deviceName = "DeepCool LS520/LS720 SE DIGITAL"; break;
            case 0x0007: deviceName = "DeepCool MORPHEUS"; break;
            case 0x0008: deviceName = "DeepCool AG400/AG620 DIGITAL"; break;
            case 0x0009: deviceName = "DeepCool MYSTIQUE 240/360"; break;
            case 0x000A: deviceName = "DeepCool LD240/LD360"; break;
            case 0x000C: deviceName = "DeepCool LP240/LP360"; break;
            case 0x000D: deviceName = "DeepCool LQ240/LQ360"; break;
            case 0x000F: deviceName = "DeepCool ASSASSIN IV VC VISION"; break;
            case 0x0010: deviceName = "DeepCool AK400 DIGITAL PRO"; break;
            case 0x0011: deviceName = "DeepCool AK500 DIGITAL PRO"; break;
            case 0x0012: deviceName = "DeepCool AK620 DIGITAL PRO"; break;
            case 0x0013: deviceName = "DeepCool CH170 DIGITAL"; break;
            case 0x0015: deviceName = "DeepCool CH360 DIGITAL"; break;
            case 0x0016: deviceName = "DeepCool CH270 DIGITAL"; break;
            case 0x001B: deviceName = "DeepCool CH690 DIGITAL"; break;
            default:
                deviceName = QString("DeepCool Device (PID: 0x%1)").arg(product, 4, 16, QChar('0'));
                break;
        }
        return true;
    }

    if (vendor == 0x34D3 && product == 0x1100) {
        deviceName = "DeepCool CH510 MESH DIGITAL";
        return true;
    }

    return false;
}

QString DeepCoolDevice::getDeviceInfo() const
{
    if (!isOpen()) {
        return "Device not open";
    }

    return QString("Name: %1\nVendor ID: 0x%2\nProduct ID: 0x%3\nPath: %4")
        .arg(deviceName)
        .arg(deviceInfo.vendorId, 4, 16, QChar('0'))
        .arg(deviceInfo.productId, 4, 16, QChar('0'))
        .arg(deviceInfo.devicePath);
}

bool DeepCoolDevice::sendData(const QByteArray &data)
{
    if (!isOpen()) {
        return false;
    }

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        int bytesWritten = ::write(fd, data.constData(), data.size());
        return (bytesWritten == data.size());
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        int bytesWritten = 0;
        int ret = libusb_bulk_transfer(
            deviceHandle, endpointOut,
            (unsigned char*)data.constData(), data.size(),
            &bytesWritten, 1000
        );
        return (ret >= 0);
    }

    return false;
}

QByteArray DeepCoolDevice::receiveData(int length)
{
    if (!isOpen()) {
        return QByteArray();
    }

    QByteArray buffer(length, 0);

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        int bytesRead = ::read(fd, buffer.data(), length);
        if (bytesRead < 0) {
            return QByteArray();
        }
        buffer.resize(bytesRead);
        return buffer;
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        int bytesRead = 0;
        int ret = libusb_bulk_transfer(
            deviceHandle, endpointIn,
            (unsigned char*)buffer.data(), length,
            &bytesRead, 1000
        );
        if (ret < 0) {
            return QByteArray();
        }
        buffer.resize(bytesRead);
        return buffer;
    }

    return QByteArray();
}

QByteArray DeepCoolDevice::buildPacket(quint8 command, const QByteArray &payload)
{
    QByteArray packet(PACKET_SIZE, 0);

    packet[0] = 0xAA;
    packet[1] = 0x2E;
    packet[2] = command;

    if (!payload.isEmpty() && payload.size() <= 39) {
        for (int i = 0; i < payload.size(); ++i) {
            packet[3 + i] = payload[i];
        }
    }

    packet[42] = 0x48;  // 'H'
    packet[43] = 0x49;  // 'I'
    packet[44] = 0x44;  // 'D'
    packet[45] = 0x43;  // 'C'

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(packet[i]);
    }
    packet[46] = static_cast<quint8>(checksum & 0xFF);
    packet[47] = static_cast<quint8>((checksum >> 8) & 0xFF);

    return packet;
}

bool DeepCoolDevice::validateResponse(const QByteArray &response)
{
    return !response.isEmpty() && response.size() >= 2;
}

bool DeepCoolDevice::sendStatusRequest()
{
    if (!isOpen()) {
        return false;
    }

    QByteArray packet(48, 0);
    packet[0] = static_cast<char>(0xAA);
    packet[1] = 0x2E;
    packet[2] = 0x10;
    packet[42] = 0x48;
    packet[43] = 0x49;
    packet[44] = 0x44;
    packet[45] = 0x43;

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(packet[i]);
    }
    packet[46] = static_cast<char>(checksum & 0xFF);
    packet[47] = static_cast<char>((checksum >> 8) & 0xFF);

    if (!sendData(packet)) {
        return false;
    }

    receiveData(64);
    return true;
}

bool DeepCoolDevice::initMachineInfoMode()
{
    if (!isOpen()) {
        return false;
    }

    // Command 0x0A was found to be valid - try it with different payloads
    qDebug() << "Testing command 0x0A with various payloads...";

    quint8 payloads[][8] = {
        {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00},
    };

    int numPayloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < numPayloads; i++) {
        QByteArray packet(48, 0);
        packet[0] = static_cast<char>(0xAA);
        packet[1] = 0x2E;
        packet[2] = 0x0A;

        for (int j = 0; j < 8; j++) {
            packet[3 + j] = payloads[i][j];
        }

        packet[42] = 0x48;
        packet[43] = 0x49;
        packet[44] = 0x44;
        packet[45] = 0x43;

        quint16 checksum = 0;
        for (int j = 0; j < 46; ++j) {
            checksum += static_cast<quint8>(packet[j]);
        }
        packet[46] = static_cast<char>(checksum & 0xFF);
        packet[47] = static_cast<char>((checksum >> 8) & 0xFF);

        qDebug() << "  0x0A payload:" << QByteArray((char*)payloads[i], 4).toHex();

        if (sendData(packet)) {
            QByteArray response = receiveData(64);
            qDebug() << "    Resp:" << response.left(10).toHex();
        }
        usleep(200000);
    }

    // Send 0x0A then check status
    qDebug() << "\nSending 0x0A modes then checking status...";

    for (int mode = 0; mode <= 5; mode++) {
        QByteArray packet(48, 0);
        packet[0] = static_cast<char>(0xAA);
        packet[1] = 0x2E;
        packet[2] = 0x0A;
        packet[3] = mode;
        packet[42] = 0x48;
        packet[43] = 0x49;
        packet[44] = 0x44;
        packet[45] = 0x43;

        quint16 checksum = 0;
        for (int j = 0; j < 46; ++j) {
            checksum += static_cast<quint8>(packet[j]);
        }
        packet[46] = static_cast<char>(checksum & 0xFF);
        packet[47] = static_cast<char>((checksum >> 8) & 0xFF);

        qDebug() << "  0x0A mode=" << mode;
        sendData(packet);
        receiveData(64);
        usleep(100000);

        // Check status
        QByteArray statusPacket(48, 0);
        statusPacket[0] = static_cast<char>(0xAA);
        statusPacket[1] = 0x2E;
        statusPacket[2] = 0x10;
        statusPacket[42] = 0x48;
        statusPacket[43] = 0x49;
        statusPacket[44] = 0x44;
        statusPacket[45] = 0x43;

        checksum = 0;
        for (int j = 0; j < 46; ++j) {
            checksum += static_cast<quint8>(statusPacket[j]);
        }
        statusPacket[46] = static_cast<char>(checksum & 0xFF);
        statusPacket[47] = static_cast<char>((checksum >> 8) & 0xFF);

        sendData(statusPacket);
        QByteArray statusResp = receiveData(64);
        qDebug() << "    Status:" << statusResp.left(10).toHex();

        usleep(200000);
    }

    qDebug() << "\nInit complete - check display";
    return false;
}

bool DeepCoolDevice::setDisplayMode(DisplayMode mode)
{
    currentMode = mode;
    return true;
}

bool DeepCoolDevice::updateDisplay(const SystemData &data)
{
    if (!isOpen()) {
        return false;
    }

    // Send status request first
    QByteArray statusPacket(48, 0);
    statusPacket[0] = static_cast<char>(0xAA);
    statusPacket[1] = 0x2E;
    statusPacket[2] = 0x10;
    statusPacket[42] = 0x48;
    statusPacket[43] = 0x49;
    statusPacket[44] = 0x44;
    statusPacket[45] = 0x43;
    quint16 statusChecksum = 0;
    for (int i = 0; i < 46; ++i) {
        statusChecksum += static_cast<quint8>(statusPacket[i]);
    }
    statusPacket[46] = static_cast<char>(statusChecksum & 0xFF);
    statusPacket[47] = static_cast<char>((statusChecksum >> 8) & 0xFF);

    if (!sendData(statusPacket)) {
        return false;
    }
    receiveData(48);

    // Get CPU frequency (max across all cores)
    quint16 cpuMhz = 0;
    QDir cpuDir("/sys/devices/system/cpu");
    QStringList cpus = cpuDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& cpu : cpus) {
        if (!cpu.startsWith("cpu") || cpu.length() < 4 || !cpu[3].isDigit()) {
            continue;
        }
        QString freqPath = QString("/sys/devices/system/cpu/%1/cpufreq/scaling_cur_freq").arg(cpu);
        QFile cpuFreqFile(freqPath);
        if (cpuFreqFile.open(QIODevice::ReadOnly)) {
            QString freqStr = QTextStream(&cpuFreqFile).readAll().trimmed();
            quint32 freqKhz = freqStr.toUInt();
            quint16 freq = static_cast<quint16>(freqKhz / 1000);
            cpuFreqFile.close();
            if (freq > cpuMhz) {
                cpuMhz = freq;
            }
        }
    }
    if (cpuMhz == 0) {
        cpuMhz = 3500;
    }

    // Get sensor values
    quint8 cpuTemp = static_cast<quint8>(qBound(0.0f, data.cpuTemp, 127.0f));
    quint8 cpuUsage = static_cast<quint8>(qBound(0.0f, data.cpuUsage, 100.0f));
    quint8 gpuTemp = static_cast<quint8>(qBound(0.0f, data.gpuTemp, 127.0f));

    // In GPU_FOCUS mode, swap CPU and GPU temps
    quint8 mainTemp = cpuTemp;
    quint8 bottomTemp = gpuTemp;

    if (currentMode == MODE_GPU_FOCUS) {
        mainTemp = gpuTemp;
        bottomTemp = cpuTemp;
    }

    // RAM usage (rounded)
    float ramBounded = qBound(0.0f, data.ramUsage, 100.0f);
    quint8 memUsage = static_cast<quint8>(qRound(ramBounded));

    // Bottom display value
    quint8 displayInteger = 0;
    quint16 displayDecimal = 0;

    if (currentMode == MODE_GPU_INFO) {
        displayInteger = gpuTemp;
        displayDecimal = 0;
    } else if (currentMode == MODE_GPU_FOCUS) {
        displayInteger = bottomTemp;
        displayDecimal = 0;
    } else {
        float ghz = cpuMhz / 1000.0f;
        displayInteger = static_cast<quint8>(ghz);
        displayDecimal = static_cast<quint16>((ghz - displayInteger) * 100) * 100;
    }

    // Calculate helper bytes
    quint8 byte9 = memUsage;
    quint8 byte11 = (gpuTemp < 35) ? 0x05 : ((gpuTemp < 38) ? 0x06 : 0x09);
    quint8 byte17 = 0x06;
    quint8 byte23 = 0;
    if (currentMode != MODE_GPU_INFO && currentMode != MODE_GPU_FOCUS) {
        byte23 = static_cast<quint8>((cpuMhz % 1000) / 10);
        if (byte23 < 10) byte23 = 0x0a;
    }

    // Build display packet
    QByteArray displayPacket(48, 0);
    displayPacket[0] = static_cast<char>(0xAA);
    displayPacket[1] = 0x2E;
    displayPacket[2] = 0x01;
    displayPacket[3] = mainTemp;
    displayPacket[6] = cpuUsage;
    displayPacket[9] = byte9;
    displayPacket[11] = byte11;
    displayPacket[12] = 0x03;
    displayPacket[14] = gpuTemp;
    displayPacket[15] = 0x05;
    displayPacket[17] = byte17;
    displayPacket[18] = 0x0c;
    displayPacket[20] = 0x07;
    displayPacket[21] = displayInteger;
    displayPacket[23] = byte23;
    displayPacket[24] = static_cast<char>(displayDecimal & 0xFF);
    displayPacket[25] = static_cast<char>((displayDecimal >> 8) & 0xFF);
    displayPacket[27] = static_cast<char>(displayDecimal & 0xFF);
    displayPacket[28] = static_cast<char>((displayDecimal >> 8) & 0xFF);
    displayPacket[42] = 0x48;
    displayPacket[43] = 0x49;
    displayPacket[44] = 0x44;
    displayPacket[45] = 0x43;

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(displayPacket[i]);
    }
    displayPacket[46] = static_cast<char>(checksum & 0xFF);
    displayPacket[47] = static_cast<char>((checksum >> 8) & 0xFF);

    if (!sendData(displayPacket)) {
        return false;
    }
    receiveData(48);

    return true;
}

bool DeepCoolDevice::setAlarm(bool enabled)
{
    if (!isOpen()) {
        return false;
    }

    QByteArray payload;
    payload.append(enabled ? 0x01 : 0x00);
    QByteArray packet = buildPacket(CMD_SET_ALARM, payload);

    return sendData(packet);
}

bool DeepCoolDevice::setUpdateInterval(int milliseconds)
{
    Q_UNUSED(milliseconds);
    return true;
}
