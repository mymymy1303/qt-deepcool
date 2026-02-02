#ifndef DEEPCOOLDEVICE_H
#define DEEPCOOLDEVICE_H

#include <QString>
#include <QByteArray>
#include <libusb-1.0/libusb.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include "device.h"

enum DisplayMode {
    MODE_CPU_INFO = 0,      // CPU temp main, CPU GHz bottom
    MODE_GPU_INFO = 1,      // CPU temp main, GPU temp bottom
    MODE_SYSTEM_OVERVIEW = 2,
    MODE_CUSTOM = 3,
    MODE_GPU_FOCUS = 4      // GPU temp main (with LED), CPU temp bottom
};

struct SystemData {
    float cpuTemp;
    float cpuUsage;
    float gpuTemp;
    float gpuUsage;
    float ramUsage;
    bool useFahrenheit;
};

class DeepCoolDevice : public Device
{
public:
    DeepCoolDevice();
    ~DeepCoolDevice() override;

    // Implement Device interface
    bool open(const DeviceInfo &deviceInfo) override;
    void close() override;
    bool isOpen() const override {
        return (deviceType == DEVICE_TYPE_HID && fd >= 0) ||
               (deviceType == DEVICE_TYPE_USB_VENDOR && deviceHandle != nullptr);
    }

    bool sendData(const QByteArray &data) override;
    QByteArray receiveData(int length) override;
    QString getDeviceName() const override { return deviceName; }
    QString getDeviceInfo() const override;

    // High-level DeepCool-specific commands
    bool sendStatusRequest();  // Handshake/init
    bool initMachineInfoMode();  // Try to switch device to Machine Info mode
    bool setDisplayMode(DisplayMode mode);
    bool updateDisplay(const SystemData &data);

    // Device verification
    bool verifyDevice();

private:
    DeviceType deviceType;

    // For USB vendor-specific devices
    libusb_context *usbContext;
    libusb_device_handle *deviceHandle;
    int interfaceNumber;
    int endpointOut;
    int endpointIn;

    // For HID devices
    int fd;  // File descriptor for HID device

    // Common
    QString devicePath;
    QString deviceName;
    DisplayMode currentMode;

    // Protocol helpers
    QByteArray buildPacket(quint8 command, const QByteArray &payload);
    bool validateResponse(const QByteArray &response);

    // Command bytes (reverse-engineered from USB capture)
    static const quint8 CMD_UPDATE_DISPLAY = 0x01;  // Send display data
    static const quint8 CMD_STATUS_REQUEST = 0x10;  // Status/handshake request

    static const int PACKET_SIZE = 48;  // MYSTIQUE uses 48-byte packets
};

#endif // DEEPCOOLDEVICE_H
