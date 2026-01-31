#ifndef DEVICE_H
#define DEVICE_H

#include <QString>
#include <QByteArray>
#include <QVector>
#include <libusb-1.0/libusb.h>
#include <linux/hidraw.h>

enum DeviceType {
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_HID = 1,        // Standard HID device (/dev/hidraw*)
    DEVICE_TYPE_USB_VENDOR = 2  // Vendor-specific USB device (libusb)
};

// Structure to hold detected device information
struct DeviceInfo {
    DeviceType type;
    uint16_t vendorId;
    uint16_t productId;
    QString devicePath;      // For HID: /dev/hidrawX, For USB: usb:vid:pid
    QString displayName;     // Human-readable name
    uint8_t busNumber;       // USB bus number (0 for HID)
    uint8_t deviceAddress;   // USB device address (0 for HID)
    
    DeviceInfo()
        : type(DEVICE_TYPE_UNKNOWN)
        , vendorId(0)
        , productId(0)
        , busNumber(0)
        , deviceAddress(0)
    {}
};

// DeepCool USB Vendor/Product IDs
#define DEEPCOOL_VENDOR_ID 0x3633
#define MYSTIQUE_360_PRODUCT_ID 0x0009

// Base class for all device types
class Device
{
public:
    virtual ~Device() = default;
    
    // Static factory method - Device detection
    static QVector<DeviceInfo> detectDevices();
    
    // Helper method to get device name from VID:PID
    static QString getDeviceNameFromIds(uint16_t vid, uint16_t pid);
    
    // Pure virtual methods that must be implemented by derived classes
    virtual bool open(const DeviceInfo &deviceInfo) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    
    // Data communication
    virtual bool sendData(const QByteArray &data) = 0;
    virtual QByteArray receiveData(int length) = 0;
    
    // Device information
    virtual QString getDeviceName() const = 0;
    virtual QString getDeviceInfo() const = 0;
    
    // Get the device info structure
    const DeviceInfo& getDeviceInfoStruct() const { return deviceInfo; }
    
protected:
    DeviceInfo deviceInfo;
    
private:
    // Internal detection methods
    static QVector<DeviceInfo> scanHidDevices();
    static QVector<DeviceInfo> scanUsbDevices();
};

#endif // DEVICE_H
