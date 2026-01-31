#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <fstream>



#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <unistd.h>



void printDeviceInfo(const std::string& devicePath) {
    int fd = open(devicePath.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "Cannot open " << devicePath << ": " << strerror(errno) << std::endl;
        return;
    }

    struct hidraw_devinfo info;
    if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
        std::cerr << "Cannot get device info for " << devicePath << std::endl;
        close(fd);
        return;
    }

    // Get device name
    char name[256] = {0};
    if (ioctl(fd, HIDIOCGRAWNAME(256), name) < 0) {
        strcpy(name, "Unknown");
    }

    // Get manufacturer
    char manufacturer[256] = {0};
    ioctl(fd, HIDIOCGRAWPHYS(256), manufacturer);

    std::cout << "\n========================================" << std::endl;
    std::cout << "Device: " << devicePath << std::endl;
    std::cout << "Name: " << name << std::endl;
    std::cout << "Manufacturer: " << manufacturer << std::endl;
    std::cout << "Vendor ID:  0x" << std::hex << std::setw(4) << std::setfill('0') 
              << info.vendor << " (" << std::dec << info.vendor << ")" << std::endl;
    std::cout << "Product ID: 0x" << std::hex << std::setw(4) << std::setfill('0') 
              << info.product << " (" << std::dec << info.product << ")" << std::endl;
    std::cout << "Bus Type: " << std::dec << info.bustype << " (";
    switch(info.bustype) {
        case 1: std::cout << "PCI"; break;
        case 2: std::cout << "ISAPNP"; break;
        case 3: std::cout << "USB"; break;
        case 4: std::cout << "HIL"; break;
        case 5: std::cout << "BLUETOOTH"; break;
        case 6: std::cout << "VIRTUAL"; break;
        default: std::cout << "UNKNOWN"; break;
    }
    std::cout << ")" << std::endl;

    // Try to get report descriptor size
    int desc_size = 0;
    if (ioctl(fd, HIDIOCGRDESCSIZE, &desc_size) >= 0) {
        std::cout << "Report Descriptor Size: " << desc_size << " bytes" << std::endl;
    }

    close(fd);
}

std::string readSysFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::string content;
    std::getline(file, content);
    return content;
}

void scanUSBDevices() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Scanning USB devices in /sys/bus/usb/devices..." << std::endl;
    std::cout << "========================================\n" << std::endl;

    DIR* dir = opendir("/sys/bus/usb/devices");
    if (!dir) {
        std::cerr << "Cannot open /sys/bus/usb/devices" << std::endl;
        return;
    }

    struct dirent* entry;
    bool foundDeepCool = false;
    
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        
        std::string basePath = "/sys/bus/usb/devices/" + name;
        std::string idVendor = readSysFile(basePath + "/idVendor");
        std::string idProduct = readSysFile(basePath + "/idProduct");
        
        if (idVendor.empty() || idProduct.empty()) continue;
        
        // Check if it's the DeepCool device
        if (idVendor == "3633" && idProduct == "0009") {
            foundDeepCool = true;
            std::cout << "\n*** FOUND DEEPCOOL MYSTIQUE! ***" << std::endl;
            std::cout << "USB Device: " << name << std::endl;
            std::cout << "Path: " << basePath << std::endl;
            std::cout << "Vendor ID: 0x" << idVendor << std::endl;
            std::cout << "Product ID: 0x" << idProduct << std::endl;
            
            std::string manufacturer = readSysFile(basePath + "/manufacturer");
            std::string product = readSysFile(basePath + "/product");
            std::string serial = readSysFile(basePath + "/serial");
            
            if (!manufacturer.empty()) std::cout << "Manufacturer: " << manufacturer << std::endl;
            if (!product.empty()) std::cout << "Product: " << product << std::endl;
            if (!serial.empty()) std::cout << "Serial: " << serial << std::endl;
            
            // Check for HID interfaces
            std::cout << "\nChecking for HID interfaces..." << std::endl;
            DIR* devDir = opendir(basePath.c_str());
            if (devDir) {
                struct dirent* devEntry;
                while ((devEntry = readdir(devDir)) != nullptr) {
                    std::string devName = devEntry->d_name;
                    if (devName.find(name + ":") == 0) {
                        std::string intfPath = basePath + "/" + devName;
                        std::string intfClass = readSysFile(intfPath + "/bInterfaceClass");
                        std::string intfSubClass = readSysFile(intfPath + "/bInterfaceSubClass");
                        std::string intfProtocol = readSysFile(intfPath + "/bInterfaceProtocol");
                        
                        std::cout << "  Interface: " << devName << std::endl;
                        if (!intfClass.empty()) {
                            std::cout << "    Class: 0x" << intfClass;
                            if (intfClass == "03") std::cout << " (HID)";
                            std::cout << std::endl;
                        }
                        if (!intfSubClass.empty()) std::cout << "    SubClass: 0x" << intfSubClass << std::endl;
                        if (!intfProtocol.empty()) std::cout << "    Protocol: 0x" << intfProtocol << std::endl;
                        
                        // Check for hidraw device
                        std::string hidPath = intfPath + "/hidraw";
                        DIR* hidDir = opendir(hidPath.c_str());
                        if (hidDir) {
                            struct dirent* hidEntry;
                            while ((hidEntry = readdir(hidDir)) != nullptr) {
                                std::string hidName = hidEntry->d_name;
                                if (hidName.find("hidraw") == 0) {
                                    std::cout << "    *** HID Device: /dev/" << hidName << " ***" << std::endl;
                                }
                            }
                            closedir(hidDir);
                        }
                    }
                }
                closedir(devDir);
            }
            std::cout << "\n========================================" << std::endl;
        }
    }
    
    closedir(dir);
    
    if (!foundDeepCool) {
        std::cout << "\n*** DeepCool MYSTIQUE (3633:0009) NOT FOUND in USB devices! ***" << std::endl;
        std::cout << "Please check:" << std::endl;
        std::cout << "1. Is the device plugged in?" << std::endl;
        std::cout << "2. Run 'lsusb | grep -i deepcool' to verify" << std::endl;
        std::cout << "========================================\n" << std::endl;
    }
}

int deepcoolDevice() {
    std::cout << "HID Device Inspector" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << "\nScanning for HID devices...\n" << std::endl;

    DIR* dir = opendir("/dev");
    if (!dir) {
        std::cerr << "Cannot open /dev directory" << std::endl;
        return 1;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name.find("hidraw") == 0) {
            printDeviceInfo("/dev/" + name);
        }
    }

    closedir(dir);

    std::cout << "\n========================================" << std::endl;
    std::cout << "\nLooking for DeepCool MYSTIQUE 360..." << std::endl;
    std::cout << "Expected VID: 0x3633 (13875)" << std::endl;
    std::cout << "Expected PID: 0x0009 (9)" << std::endl;
    std::cout << "\nIf none match, the device might be using a different VID." << std::endl;
    std::cout << "Check the 'Name' field for 'DeepCool' or 'MYSTIQUE'" << std::endl;
    
    // Now scan USB devices to find the DeepCool device
    scanUSBDevices();

    return 0;
}

int main(int argc, char *argv[])
{
    // deepcoolDevice();
    scanUSBDevices();
    QApplication a(argc, argv);

    // Check if running with root privileges
    if (geteuid() != 0) {
        QMessageBox::warning(nullptr, "Permission Required",
                             "This application requires root privileges to access HID devices.\n\n"
                             "Please run with: sudo ./deepcool-qt");
        // Continue anyway for UI testing, but device access will fail
    }

    MainWindow w;
    w.show();
    return a.exec();
}

