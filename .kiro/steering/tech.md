# Technology Stack

## Language & Standard
- C++17
- Qt 6 Core (CLI only)

## Build System
- CMake 3.16+
- Qt MOC auto-generation enabled

## Dependencies
- Qt6::Core
- libusb-1.0 (vendor-specific USB device access)
- libudev (Linux device detection)

## Platform
- Linux only (uses /sys, /proc, hidraw, udev)

## Common Commands

```bash
# Build
cmake -B build -S .
cmake --build build

# Run
sudo ./build/bin/deepcool-cli --list
sudo ./build/bin/deepcool-cli --mode cpu --interval 1000 -V

# Clean rebuild
rm -rf build && cmake -B build -S . && cmake --build build
```

## Device Access
Requires either:
- Root privileges (`sudo`)
- udev rules in `/etc/udev/rules.d/99-deepcool.rules`
