#include "deepcooldevice.h"
#include <QDebug>
#include <QDir>
#include <QFile>
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
    , endpointOut(0x02)  // Back to endpoint 2 - matches USB capture
    , endpointIn(0x82)
    , fd(-1)
    , currentMode(MODE_CPU_INFO)
{
    int ret = libusb_init(&usbContext);
    if (ret < 0) {
        qWarning() << "Failed to initialize libusb:" << libusb_error_name(ret);
    }
}

DeepCoolDevice::~DeepCoolDevice()
{
    close();
    if (usbContext) {
        libusb_exit(usbContext);
        usbContext = nullptr;
    }
}

// Implement Device interface - new open() method
bool DeepCoolDevice::open(const DeviceInfo &devInfo)
{
    if (isOpen()) {
        close();
    }

    // Store device info
    deviceInfo = devInfo;
    deviceType = devInfo.type;
    deviceName = devInfo.displayName;
    openedViaLegacyMethod = false;

    qDebug() << "Opening device:" << devInfo.displayName << "at" << devInfo.devicePath;

    if (devInfo.type == DEVICE_TYPE_HID) {
        // Open HID device
        fd = ::open(devInfo.devicePath.toUtf8().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            qWarning() << "Failed to open HID device:" << devInfo.devicePath
                       << "Error:" << strerror(errno);
            return false;
        }

        qDebug() << "Opened HID device:" << devInfo.devicePath;
    }
    else if (devInfo.type == DEVICE_TYPE_USB_VENDOR) {
        // Open USB vendor device by VID:PID
        if (!usbContext) {
            qWarning() << "USB context not initialized";
            return false;
        }

        qDebug() << "Attempting to open USB device with VID:0x" << QString::number(devInfo.vendorId, 16)
                 << "PID:0x" << QString::number(devInfo.productId, 16);

        // Find and open the device by VID:PID
        libusb_device **devList;
        ssize_t cnt = libusb_get_device_list(usbContext, &devList);
        if (cnt < 0) {
            qWarning() << "Failed to get device list:" << libusb_error_name(cnt);
            return false;
        }

        libusb_device *targetDevice = nullptr;
        for (ssize_t i = 0; i < cnt; i++) {
            struct libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(devList[i], &desc) == 0) {
                if (desc.idVendor == devInfo.vendorId && desc.idProduct == devInfo.productId) {
                    targetDevice = devList[i];
                    uint8_t bus = libusb_get_bus_number(devList[i]);
                    uint8_t addr = libusb_get_device_address(devList[i]);
                    qDebug() << "Found matching device at bus" << bus << "address" << addr;
                    break;
                }
            }
        }

        if (!targetDevice) {
            qWarning() << "Device with VID:0x" << QString::number(devInfo.vendorId, 16)
                       << "PID:0x" << QString::number(devInfo.productId, 16) << "not found";
            libusb_free_device_list(devList, 1);
            return false;
        }

        // Try to open the device
        int ret = libusb_open(targetDevice, &deviceHandle);
        libusb_free_device_list(devList, 1);

        if (ret < 0) {
            qWarning() << "Failed to open USB device:" << libusb_error_name(ret);

            if (ret == LIBUSB_ERROR_ACCESS) {
                qWarning() << "\n=== PERMISSION DENIED ===";
                qWarning() << "The device was found but cannot be opened due to permissions.";
                qWarning() << "\nPossible solutions:";
                qWarning() << "1. Make sure you're running with sudo: sudo ./deepcool-qt";
                qWarning() << "2. A kernel driver may have claimed the device";
                qWarning() << "3. Create udev rules: /etc/udev/rules.d/99-deepcool.rules";
                qWarning() << "   SUBSYSTEM==\"usb\", ATTR{idVendor}==\"3633\", ATTR{idProduct}==\"0009\", MODE=\"0666\"";
            } else if (ret == LIBUSB_ERROR_BUSY) {
                qWarning() << "Device is busy - another program may be using it";
            } else if (ret == LIBUSB_ERROR_NO_DEVICE) {
                qWarning() << "Device disappeared during open";
            }

            return false;
        }

        qDebug() << "Successfully opened USB device";

        // Detach kernel driver if active
        if (libusb_kernel_driver_active(deviceHandle, interfaceNumber) == 1) {
            qDebug() << "Detaching kernel driver...";
            ret = libusb_detach_kernel_driver(deviceHandle, interfaceNumber);
            if (ret < 0) {
                qWarning() << "Failed to detach kernel driver:" << libusb_error_name(ret);
            }
        }

        // Claim the interface
        ret = libusb_claim_interface(deviceHandle, interfaceNumber);
        if (ret < 0) {
            qWarning() << "Failed to claim interface:" << libusb_error_name(ret);
            close();
            return false;
        }

        qDebug() << "Opened USB vendor device with VID:0x" << QString::number(devInfo.vendorId, 16)
                 << "PID:0x" << QString::number(devInfo.productId, 16);
    }
    else {
        qWarning() << "Unknown device type";
        return false;
    }

    // Verify it's a DeepCool device
    if (!verifyDevice()) {
        close();
        return false;
    }

    // Skip status request on open - keep it simple
    qDebug() << "Successfully opened device:" << deviceInfo.displayName;
    return true;
}

// Implement Device interface - new close() method
void DeepCoolDevice::close()
{
    if (deviceType == DEVICE_TYPE_HID && fd >= 0) {
        ::close(fd);
        fd = -1;
        qDebug() << "HID device closed:" << deviceInfo.devicePath;
    }
    else if (deviceType == DEVICE_TYPE_USB_VENDOR && deviceHandle) {
        libusb_release_interface(deviceHandle, interfaceNumber);
        libusb_close(deviceHandle);
        deviceHandle = nullptr;
        qDebug() << "USB device closed:" << deviceInfo.devicePath;
    }

    deviceType = DEVICE_TYPE_UNKNOWN;
    openedViaLegacyMethod = false;
}

// Legacy openDevice() method for backward compatibility
bool DeepCoolDevice::openDevice(const DeviceInfo &deviceInfo)
{

    return open(deviceInfo);
}

// Legacy closeDevice() method
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

    // Get vendor/product ID based on device type
    if (deviceInfo.type == DEVICE_TYPE_HID) {
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            qWarning() << "Failed to get device info";
            return false;
        }
        vendor = info.vendor;
        product = info.product;
        qDebug() << "HID Device info - Vendor:" << QString::number(vendor, 16)
                 << "Product:" << QString::number(product, 16)
                 << "Bus:" << info.bustype;
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        // For USB vendor devices, use the stored deviceInfo
        vendor = deviceInfo.vendorId;
        product = deviceInfo.productId;
        qDebug() << "USB Device info - Vendor:" << QString::number(vendor, 16)
                 << "Product:" << QString::number(product, 16);
    } else {
        qWarning() << "Unknown device type";
        return false;
    }

    // Check vendor ID and product ID
    if (vendor == DEEPCOOL_VENDOR_ID) {
        // Identify specific model based on PID
        switch (product) {
            case 0x0001:
                deviceName = "DeepCool AK400 DIGITAL";
                break;
            case 0x0002:
                deviceName = "DeepCool AK620 DIGITAL";
                break;
            case 0x0003:
                deviceName = "DeepCool AK500 DIGITAL";
                break;
            case 0x0004:
                deviceName = "DeepCool AK500S DIGITAL";
                break;
            case 0x0005:
                deviceName = "DeepCool CH560 DIGITAL";
                break;
            case 0x0006:
                deviceName = "DeepCool LS520/LS720 SE DIGITAL";
                break;
            case 0x0007:
                deviceName = "DeepCool MORPHEUS";
                break;
            case 0x0008:
                deviceName = "DeepCool AG400/AG620 DIGITAL";
                break;
            case 0x0009:
                deviceName = "DeepCool MYSTIQUE 240/360";
                break;
            case 0x000A:
                deviceName = "DeepCool LD240/LD360";
                break;
            case 0x000C:
                deviceName = "DeepCool LP240/LP360";
                break;
            case 0x000D:
                deviceName = "DeepCool LQ240/LQ360";
                break;
            case 0x000F:
                deviceName = "DeepCool ASSASSIN IV VC VISION";
                break;
            case 0x0010:
                deviceName = "DeepCool AK400 DIGITAL PRO";
                break;
            case 0x0011:
                deviceName = "DeepCool AK500 DIGITAL PRO";
                break;
            case 0x0012:
                deviceName = "DeepCool AK620 DIGITAL PRO";
                break;
            case 0x0013:
                deviceName = "DeepCool CH170 DIGITAL";
                break;
            case 0x0015:
                deviceName = "DeepCool CH360 DIGITAL";
                break;
            case 0x0016:
                deviceName = "DeepCool CH270 DIGITAL";
                break;
            case 0x001B:
                deviceName = "DeepCool CH690 DIGITAL";
                break;
            default:
                deviceName = QString("DeepCool Device (PID: 0x%1)")
                    .arg(product, 4, 16, QChar('0'));
                qWarning() << "Unknown DeepCool product ID:" << product;
                break;
        }
        return true;
    }

    // Special case: CH510 MESH DIGITAL has different vendor ID
    if (vendor == 0x34D3 && product == 0x1100) {
        deviceName = "DeepCool CH510 MESH DIGITAL";
        return true;
    }

    qWarning() << "Not a DeepCool device";
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
        qWarning() << "Device not open";
        return false;
    }

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        // HID device - use write()
        int bytesWritten = ::write(fd, data.constData(), data.size());
        if (bytesWritten < 0) {
            qWarning() << "Failed to write to HID device:" << strerror(errno);
            return false;
        }

        if (bytesWritten != data.size()) {
            qWarning() << "Partial write:" << bytesWritten << "of" << data.size() << "bytes";
            return false;
        }

        qDebug() << "Sent" << bytesWritten << "bytes to HID device";
        return true;

    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        // USB vendor device - MYSTIQUE uses vendor-specific protocol
        // Based on research: The device needs special initialization
        // Try bulk transfer first (most common for vendor-specific devices)
        int bytesWritten = 0;
        int ret = libusb_bulk_transfer(
            deviceHandle,
            endpointOut,
            (unsigned char*)data.constData(),
            data.size(),
            &bytesWritten,
            1000  // 1 second timeout
        );

        if (ret < 0) {
            qWarning() << "Failed to write to USB device (bulk transfer):" << libusb_error_name(ret);
            qWarning() << "Note: MYSTIQUE protocol is not fully reverse-engineered yet.";
            qWarning() << "See: https://github.com/Nortank12/deepcool-digital-linux/discussions/18";
            return false;
        }

        qDebug() << "Sent" << bytesWritten << "bytes to USB device (bulk)";
        return true;
    }

    qWarning() << "Unknown device type";
    return false;
}

QByteArray DeepCoolDevice::receiveData(int length)
{
    if (!isOpen()) {
        qWarning() << "Device not open";
        return QByteArray();
    }

    QByteArray buffer(length, 0);

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        // HID device - use read()
        int bytesRead = ::read(fd, buffer.data(), length);

        if (bytesRead < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No data available (non-blocking mode)
                return QByteArray();
            }
            qWarning() << "Failed to read from HID device:" << strerror(errno);
            return QByteArray();
        }

        buffer.resize(bytesRead);
        qDebug() << "Received" << bytesRead << "bytes from HID device";
        return buffer;

    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        // USB vendor device - use bulk transfer (same as send)
        int bytesRead = 0;
        int ret = libusb_bulk_transfer(
            deviceHandle,
            endpointIn,
            (unsigned char*)buffer.data(),
            length,
            &bytesRead,
            1000  // 1 second timeout
        );

        if (ret < 0) {
            if (ret == LIBUSB_ERROR_TIMEOUT) {
                // Timeout - no data available
                return QByteArray();
            }
            qWarning() << "Failed to read from USB device:" << libusb_error_name(ret);
            return QByteArray();
        }

        buffer.resize(bytesRead);
        qDebug() << "Received" << bytesRead << "bytes from USB device";
        return buffer;
    }

    qWarning() << "Unknown device type";
    return QByteArray();
}

QByteArray DeepCoolDevice::buildPacket(quint8 command, const QByteArray &payload)
{
    QByteArray packet;
    packet.resize(PACKET_SIZE);
    packet.fill(0);

    // MYSTIQUE protocol based on USB capture:
    // Bytes 0-1: Header (0xAA 0x2E)
    packet[0] = 0xAA;
    packet[1] = 0x2E;

    // Byte 2: Command
    packet[2] = command;

    // Bytes 3-40: Payload data (38 bytes available)
    if (!payload.isEmpty() && payload.size() <= 38) {
        for (int i = 0; i < payload.size(); ++i) {
            packet[3 + i] = payload[i];
        }
    }

    // Bytes 41-44: Footer "HIDC" (0x48 0x49 0x44 0x43)
    packet[41] = 0x48;  // 'H'
    packet[42] = 0x49;  // 'I'
    packet[43] = 0x44;  // 'D'
    packet[44] = 0x43;  // 'C'

    // Bytes 45-46: Checksum (simple sum of bytes 0-44, little-endian)
    quint16 checksum = 0;
    for (int i = 0; i < 45; ++i) {
        checksum += static_cast<quint8>(packet[i]);
    }
    packet[45] = static_cast<quint8>(checksum & 0xFF);         // Low byte first
    packet[46] = static_cast<quint8>((checksum >> 8) & 0xFF);  // High byte second

    // Byte 47: Reserved/padding
    packet[47] = 0x02;

    return packet;
}

bool DeepCoolDevice::validateResponse(const QByteArray &response)
{
    if (response.isEmpty() || response.size() < 2) {
        return false;
    }

    // TODO: Implement actual response validation based on protocol
    // Check for ACK, error codes, etc.

    return true;
}

bool DeepCoolDevice::sendStatusRequest()
{
    if (!isOpen()) {
        return false;
    }

    // Build status packet matching exact Windows format
    QByteArray packet(48, 0);
    packet[0] = static_cast<char>(0xAA);
    packet[1] = 0x2E;
    packet[2] = 0x10;  // Status request command
    // Bytes 3-40 are zeros (payload)
    packet[41] = 0x48;  // 'H'
    packet[42] = 0x49;  // 'I'
    packet[43] = 0x44;  // 'D'
    packet[44] = 0x43;  // 'C'
    // Checksum: 0xAA + 0x2E + 0x10 + 0x48 + 0x49 + 0x44 + 0x43 = 0x200
    packet[45] = 0x00;  // Low byte
    packet[46] = 0x02;  // High byte
    packet[47] = 0x00;  // Reserved

    qDebug() << "Sending status packet:" << packet.toHex();

    if (!sendData(packet)) {
        qWarning() << "Failed to send status request";
        return false;
    }

    // Read response
    QByteArray response = receiveData(48);
    if (!response.isEmpty()) {
        qDebug() << "Status response:" << response.toHex();
    }

    return true;
}

bool DeepCoolDevice::setDisplayMode(DisplayMode mode)
{
    if (!isOpen()) {
        return false;
    }

    QByteArray payload;
    payload.append(static_cast<quint8>(mode));

    QByteArray packet = buildPacket(CMD_SET_MODE, payload);

    if (!sendData(packet)) {
        qWarning() << "Failed to send display mode command";
        return false;
    }

    currentMode = mode;
    qDebug() << "Display mode set to:" << mode;
    return true;
}

bool DeepCoolDevice::updateDisplay(const SystemData &data)
{
    if (!isOpen()) {
        return false;
    }

    // MYSTIQUE 360 uses a completely different protocol than other DeepCool devices!
    // Based on working Rust code from GitHub discussion:
    // - Uses endpoint 0x01 (not 0x02)
    // - Packet starts with 0x1b 0x00 (not 0xaa 0x2e)
    // - Different packet structure entirely

    // Build MYSTIQUE-specific packet (34 bytes based on working Rust implementation)
    QByteArray packet;
    packet.resize(34);
    packet.fill(0);

    // Header and control bytes (from working Rust implementation)
    packet[0] = 0x1b;
    packet[1] = 0x00;
    packet[2] = static_cast<char>(0xf0);
    packet[3] = 0x16;
    packet[4] = static_cast<char>(0xcc);
    packet[5] = 0x21;
    packet[6] = 0x01;
    packet[7] = static_cast<char>(0xe6);
    packet[8] = static_cast<char>(0xff);
    packet[9] = static_cast<char>(0xff);
    packet[10] = 0x00;
    packet[11] = 0x00;
    packet[12] = 0x00;
    packet[13] = 0x00;
    packet[14] = 0x09;
    packet[15] = 0x00;
    packet[16] = 0x00;
    packet[17] = 0x02;
    packet[18] = 0x00;
    packet[19] = 0x02;
    packet[20] = 0x00;
    packet[21] = 0x01;
    packet[22] = 0x03;
    packet[23] = 0x40;
    packet[24] = 0x00;
    packet[25] = 0x00;
    packet[26] = 0x00;
    // RGB color bytes (27-29) - use temperature to set color
    quint8 cpuTemp = static_cast<quint8>(qBound(0.0f, data.cpuTemp, 100.0f));
    // Color based on temp: green (cool) -> yellow -> red (hot)
    quint8 r = (cpuTemp > 50) ? 255 : (cpuTemp * 5);
    quint8 g = (cpuTemp < 50) ? 255 : (255 - (cpuTemp - 50) * 5);
    quint8 b = 0;
    packet[27] = static_cast<char>(r);
    packet[28] = static_cast<char>(g);
    packet[29] = static_cast<char>(b);
    // Additional data bytes
    packet[30] = static_cast<char>(0xf5);
    packet[31] = static_cast<char>(0xf6);
    packet[32] = static_cast<char>(0xf7);
    packet[33] = 0x00;

    qDebug() << "MYSTIQUE packet (EP1):" << packet.toHex();

    // Send on endpoint 0x01 (MYSTIQUE-specific)
    if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR && deviceHandle) {
        int bytesWritten = 0;
        int ret = libusb_bulk_transfer(
            deviceHandle,
            0x01,  // Endpoint 1 OUT - MYSTIQUE uses this!
            (unsigned char*)packet.data(),
            packet.size(),
            &bytesWritten,
            1000
        );
        if (ret < 0) {
            qWarning() << "Failed to send MYSTIQUE packet:" << libusb_error_name(ret);
            return false;
        }
        qDebug() << "Sent" << bytesWritten << "bytes to MYSTIQUE on EP1";
    }

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

    if (!sendData(packet)) {
        qWarning() << "Failed to send alarm command";
        return false;
    }

    qDebug() << "Alarm" << (enabled ? "enabled" : "disabled");
    return true;
}

bool DeepCoolDevice::setUpdateInterval(int milliseconds)
{
    // This might be a client-side setting only
    // Some devices might support setting update interval on the device itself
    qDebug() << "Update interval set to:" << milliseconds << "ms";
    return true;
}