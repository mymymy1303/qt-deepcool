# Technology Stack

## Language & Standard
- C++17
- Qt 6 framework (Core for CLI, Widgets for GUI)

## Build System
- CMake 3.16+
- Qt MOC/UIC/RCC auto-generation enabled

## Dependencies
- Qt6::Core (required)
- Qt6::Widgets (GUI only)
- libusb-1.0 (vendor-specific USB device access)
- libudev (Linux device detection)

## Platform
- Linux only (uses /sys, /proc, hidraw, udev)

## Common Commands

```bash
# Configure (CLI only - default)
cmake -B build -S .

# Configure with GUI
cmake -B build -S . -DBUILD_GUI=ON

# Build
cmake --build build

# Run CLI (requires root or udev rules)
sudo ./build/bin/deepcool-cli --list
sudo ./build/bin/deepcool-cli --mode cpu --interval 1000

# Run GUI
sudo ./build/bin/deepcool-gui

# Clean rebuild
rm -rf build && cmake -B build -S . && cmake --build build
```

## Build Options
- `BUILD_CLI=ON` (default) - Headless CLI application
- `BUILD_GUI=OFF` (default) - Qt Widgets GUI application

## Device Access
Requires either:
- Root privileges (`sudo`)
- udev rules in `/etc/udev/rules.d/99-deepcool.rules`
