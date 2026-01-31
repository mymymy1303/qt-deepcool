#include "device.h"
#include <QDir>
#include <QDebug>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

QString Device::getDeviceNameFromIds(uint16_t vid, uint16_t pid)
{
    if (vid == DEEPCOOL_VENDOR_ID) {
        switch (pid) {
            case 0x0001: return "DeepCool AK400 DIGITAL";
            case 0x0002: return "DeepCool AK620 DIGITAL";
            case 0x0003: return "DeepCool AK500 DIGITAL";
            case 0x0004: return "DeepCool AK500S DIGITAL";
            case 0x0005: return "DeepCool CH560 DIGITAL";
            case 0x0006: return "DeepCool LS520/LS720 SE DIGITAL";
            case 0x0007: return "DeepCool MORPHEUS";
            case 0x0008: return "DeepCool AG400/AG620 DIGITAL";
            case 0x0009: return "DeepCool MYSTIQUE";
            case 0x000A: return "DeepCool CH510 DIGITAL";
            default: return QString("DeepCool Device (PID: 0x%1)").arg(pid, 4, 16, QChar('0'));
        }
    }
    else if (vid == 0x34D3 && pid == 0x1100) {
        return "DeepCool CH510 MESH DIGITAL";
    }
    
    return QString("Unknown Device (VID: 0x%1, PID: 0x%2)")
        .arg(vid, 4, 16, QChar('0'))
        .arg(pid, 4, 16, QChar('0'));
}

QVector<DeviceInfo> Device::detectDevices()
{
    QVector<DeviceInfo> devices;
    
    qDebug() << "=== Starting Device Detection ===";
    
    // Scan HID devices
    QVector<DeviceInfo> hidDevices = scanHidDevices();
    devices.append(hidDevices);
    
    // Scan USB vendor devices
    QVector<DeviceInfo> usbDevices = scanUsbDevices();
    devices.append(usbDevices);
    
    qDebug() << "=== Detection Complete: Found" << devices.size() << "device(s) ===";
    
    return devices;
}

QVector<DeviceInfo> Device::scanHidDevices()
{
    QVector<DeviceInfo> devices;
    
    qDebug() << "Scanning for HID devices...";
    QDir devDir("/dev");
    
    // Get all hidraw devices
    QStringList hidrawDevices = devDir.entryList(QStringList() << "hidraw*", QDir::System);
    
    for (const QString &device : hidrawDevices) {
        QString devicePath = "/dev/" + device;
        
        // Try to open and check vendor/product ID
        int testFd = ::open(devicePath.toUtf8().constData(), O_RDWR | O_NONBLOCK);
        if (testFd < 0) {
            qDebug() << "  Cannot open" << devicePath << ":" << strerror(errno);
            continue;
        }
        
        struct hidraw_devinfo info;
        if (ioctl(testFd, HIDIOCGRAWINFO, &info) >= 0) {
            qDebug() << "  Checking" << devicePath 
                     << "VID:" << QString("0x%1").arg(info.vendor, 4, 16, QChar('0'))
                     << "PID:" << QString("0x%1").arg(info.product, 4, 16, QChar('0'))
                     << "BusType:" << info.bustype;
            
            // Check if it's a DeepCool device
            if (info.vendor == DEEPCOOL_VENDOR_ID || 
                (info.vendor == 0x34D3 && info.product == 0x1100)) {
                
                DeviceInfo devInfo;
                devInfo.type = DEVICE_TYPE_HID;
                devInfo.vendorId = info.vendor;
                devInfo.productId = info.product;
                devInfo.devicePath = devicePath;
                devInfo.displayName = getDeviceNameFromIds(info.vendor, info.product);
                devInfo.busNumber = 0;
                devInfo.deviceAddress = 0;
                
                qDebug() << "  ✓ Found HID device:" << devInfo.displayName
                         << "at" << devicePath;
                
                devices.append(devInfo);
            }
        }
        
        ::close(testFd);
    }
    
    return devices;
}

QVector<DeviceInfo> Device::scanUsbDevices()
{
    QVector<DeviceInfo> devices;
    
    qDebug() << "Scanning for USB vendor-specific devices...";
    
    libusb_context *ctx = nullptr;
    int ret = libusb_init(&ctx);
    if (ret < 0) {
        qWarning() << "Failed to initialize libusb:" << libusb_error_name(ret);
        return devices;
    }
    
    libusb_device **devList;
    ssize_t cnt = libusb_get_device_list(ctx, &devList);
    if (cnt < 0) {
        qWarning() << "Failed to get device list:" << libusb_error_name(cnt);
        libusb_exit(ctx);
        return devices;
    }
    
    for (ssize_t i = 0; i < cnt; i++) {
        libusb_device *device = devList[i];
        struct libusb_device_descriptor desc;
        
        ret = libusb_get_device_descriptor(device, &desc);
        if (ret < 0) {
            continue;
        }
        
        // Check if it's a DeepCool device (VID: 0x3633)
        if (desc.idVendor == DEEPCOOL_VENDOR_ID || 
            (desc.idVendor == 0x34D3 && desc.idProduct == 0x1100)) {
            
            uint8_t bus = libusb_get_bus_number(device);
            uint8_t addr = libusb_get_device_address(device);
            
            // Check if we already have this device (by VID:PID)
            bool alreadyAdded = false;
            for (const DeviceInfo &existing : devices) {
                if (existing.vendorId == desc.idVendor && 
                    existing.productId == desc.idProduct &&
                    existing.type == DEVICE_TYPE_USB_VENDOR) {
                    alreadyAdded = true;
                    break;
                }
            }
            
            if (!alreadyAdded) {
                DeviceInfo devInfo;
                devInfo.type = DEVICE_TYPE_USB_VENDOR;
                devInfo.vendorId = desc.idVendor;
                devInfo.productId = desc.idProduct;
                devInfo.devicePath = QString("usb:%1:%2")
                    .arg(desc.idVendor, 4, 16, QChar('0'))
                    .arg(desc.idProduct, 4, 16, QChar('0'));
                devInfo.displayName = getDeviceNameFromIds(desc.idVendor, desc.idProduct);
                devInfo.busNumber = bus;
                devInfo.deviceAddress = addr;
                
                qDebug() << " Found USB vendor device:" << devInfo.displayName
                         << "VID:PID" << devInfo.devicePath
                         << "at bus" << bus << "address" << addr;
                
                devices.append(devInfo);
            }
        }
    }
    
    libusb_free_device_list(devList, 1);
    libusb_exit(ctx);
    
    return devices;
}
