# Project Structure

```
deepcool-cli/
├── CMakeLists.txt          # Build configuration
├── build_instructions.md   # Setup and build guide
├── PROTOCOL.md             # USB protocol documentation
│
├── device.h/cpp            # Base Device class, device detection (HID + USB)
├── deepcooldevice.h/cpp    # DeepCool-specific protocol implementation
├── main_cli.cpp            # CLI entry point
│
├── 99-deepcool.rules       # udev rules for non-root access
└── deepcool-cli.service    # systemd service file
```

## Architecture

### Class Hierarchy
- `Device` (abstract base) - Detection, open/close, send/receive
  - `DeepCoolDevice` - Protocol implementation, display commands

### Device Types
- `DEVICE_TYPE_HID` - Standard HID via `/dev/hidraw*`
- `DEVICE_TYPE_USB_VENDOR` - Vendor-specific via libusb

### Protocol (48-byte packets)
- Bytes 0-1: Header `0xAA 0x2E`
- Byte 2: Command
- Bytes 3-41: Payload
- Bytes 42-45: Footer `HIDC`
- Bytes 46-47: Checksum (little-endian)

## Key Constants
- `DEEPCOOL_VENDOR_ID = 0x3633`
- `MYSTIQUE_360_PRODUCT_ID = 0x0009`
- `PACKET_SIZE = 48`
