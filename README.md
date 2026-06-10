# DeepCool MYSTIQUE 360 CLI

Linux command-line tool for controlling DeepCool MYSTIQUE 360 AIO cooler LCD display.

![License](https://img.shields.io/badge/license-MIT-blue.svg)

## Features

- Display real-time CPU/GPU temperature, usage, and RAM on the LCD screen
- Headless operation for servers (no GUI required)
- Automatic device initialization after cold boot
- Multiple display modes: CPU focus, GPU focus, GPU temperature
- Systemd service support for auto-start

## Supported Devices

- DeepCool MYSTIQUE 360 (VID: 0x3633, PID: 0x0009)

## Requirements

- Linux (tested on Ubuntu 24.04)
- Qt 6 Core
- libusb-1.0
- libudev

## Installation

### Dependencies

Ubuntu/Debian:
```bash
sudo apt install build-essential cmake qt6-base-dev libusb-1.0-0-dev libudev-dev
```

Arch Linux:
```bash
sudo pacman -S base-devel cmake qt6-base libusb
```

Fedora:
```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel libusb1-devel systemd-devel
```

### Build

```bash
cmake -B build -S .
cmake --build build
```

### Install (Optional)

```bash
sudo cmake --install build
```

## Usage

```bash
# List detected devices
sudo ./build/bin/deepcool-cli --list

# Run with CPU mode (default)
sudo ./build/bin/deepcool-cli --mode cpu --interval 1000

# Run with GPU focus mode
sudo ./build/bin/deepcool-cli --mode gpu_focus --interval 1000

# Verbose output
sudo ./build/bin/deepcool-cli --mode cpu --interval 1000 -V
```

### Options

| Option | Description |
|--------|-------------|
| `-l, --list` | List available devices and exit |
| `-d, --device <path>` | Device path or index (default: 0) |
| `-i, --interval <ms>` | Update interval in milliseconds (default: 1000) |
| `-m, --mode <mode>` | Display mode: cpu, gpu, gpu_focus |
| `-f, --fahrenheit` | Use Fahrenheit instead of Celsius |
| `-V, --verbose` | Enable verbose output |
| `-D, --daemon` | Run as daemon (fork to background) |
| `-h, --help` | Show help message |

## Running Without sudo

Install udev rules:
```bash
sudo cp 99-deepcool.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo usermod -aG plugdev $USER
# Log out and back in
```

## Systemd Service

```bash
sudo cp deepcool-cli.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now deepcool-cli
```

## Protocol

See [PROTOCOL.md](PROTOCOL.md) for USB protocol documentation, and
[REVERSE_ENGINEERING.md](REVERSE_ENGINEERING.md) for the method and tooling
used to obtain it (useful for reverse-engineering the remaining features:
GIF/video, unknown commands).

## Credits

Inspired by [Qt-deepcool-digital](https://gitlab.com/public_projects5782906/Qt-deepcool-digital) by public_projects5782906.

## License

MIT License - see [LICENSE](LICENSE) for details.
