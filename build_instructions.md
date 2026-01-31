# DeepCool Qt - Build Instructions

## Prerequisites

### Ubuntu/Debian
```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev libudev-dev
```

### Arch Linux
```bash
sudo pacman -S base-devel cmake qt6-base
```

### Fedora
```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel systemd-devel
```

## Project Structure

```
deepcool-qt/
├── CMakeLists.txt
├── main.cpp
├── mainwindow.h
├── mainwindow.cpp
├── deepcooldevice.h
└── deepcooldevice.cpp
```

## Building

### 1. Create build directory
```bash
mkdir build
cd build
```

### 2. Configure with CMake
```bash
cmake ..
```

### 3. Build the project
```bash
cmake --build .
# or simply
make
```

### 4. Run the application
```bash
# You need root privileges to access HID devices
sudo ./bin/deepcool-qt
```

## Alternative: Out-of-source build
```bash
# From project root
cmake -B build -S .
cmake --build build
sudo ./build/bin/deepcool-qt
```

## Installation (Optional)
```bash
cd build
sudo make install
# Now you can run from anywhere:
sudo deepcool-qt
```

## Setting Up udev Rules (Recommended)

To run without sudo, create a udev rule:

```bash
sudo nano /etc/udev/rules.d/99-deepcool.rules
```

Add this content (adjust vendor/product IDs for your device):
```
# DeepCool HID raw devices
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="3633", MODE="0666"

# Add more product IDs if needed
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="3633", ATTRS{idProduct}=="XXXX", MODE="0666"
```

Then reload udev rules:
```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Now you can run without sudo:
```bash
./bin/deepcool-qt
```

## Troubleshooting

### Qt6 not found
If CMake can't find Qt6, you may need to specify the path:
```bash
cmake -DCMAKE_PREFIX_PATH=/usr/lib/qt6 ..
```

### Permission denied when accessing /dev/hidraw*
- Make sure you're running with sudo, OR
- Set up udev rules as described above, OR
- Add your user to the appropriate group:
```bash
sudo usermod -a -G plugdev $USER
# Log out and log back in
```

### Device not detected
1. Check if device is connected:
```bash
lsusb
ls -la /dev/hidraw*
```

2. Check device permissions:
```bash
cat /sys/class/hidraw/hidraw*/device/uevent
```

3. Verify vendor/product IDs in the code match your device

## Development Tips

### Enable debug output
The application uses qDebug() for logging. Run with:
```bash
QT_LOGGING_RULES="*.debug=true" sudo ./bin/deepcool-qt
```

### Clean build
```bash
rm -rf build
mkdir build
cd build
cmake ..
make
```

### Quick rebuild after code changes
```bash
cd build
make
```

## Next Steps

1. **Find your MYSTIQUE 360 IDs**: Run `lsusb` and update the IDs in `deepcooldevice.h`
2. **Reverse engineer the protocol**: Study the Rust implementation at https://github.com/Nortank12/deepcool-digital-linux
3. **Test carefully**: Start with device detection, then add features gradually
4. **Report issues**: If you discover the protocol, consider contributing back to the original project!
