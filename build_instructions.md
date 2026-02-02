# DeepCool CLI - Build Instructions

## Prerequisites

### Ubuntu/Debian
```bash
sudo apt install build-essential cmake qt6-base-dev libusb-1.0-0-dev libudev-dev
```

### Arch Linux
```bash
sudo pacman -S base-devel cmake qt6-base libusb
```

### Fedora
```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel libusb1-devel systemd-devel
```

## Build

```bash
cmake -B build -S .
cmake --build build
```

## Run

```bash
# List devices
sudo ./build/bin/deepcool-cli --list

# Run with CPU mode (default)
sudo ./build/bin/deepcool-cli --mode cpu --interval 1000

# Run with verbose output
sudo ./build/bin/deepcool-cli --mode cpu --interval 1000 -V
```

## Install (Optional)

```bash
sudo cmake --install build
```

## udev Rules (Run Without sudo)

```bash
sudo cp 99-deepcool.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Then add your user to the `plugdev` group:
```bash
sudo usermod -aG plugdev $USER
# Log out and back in
```

## Systemd Service

```bash
sudo cp deepcool-cli.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now deepcool-cli
```

Check status:
```bash
sudo systemctl status deepcool-cli
journalctl -u deepcool-cli -f
```
