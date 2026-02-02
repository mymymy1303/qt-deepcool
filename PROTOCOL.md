# DeepCool MYSTIQUE Protocol Documentation

This document describes the USB protocol used by DeepCool MYSTIQUE 240/360 AIO coolers to display system information on their LCD screens.

## Device Information

| Property | Value |
|----------|-------|
| Vendor ID | `0x3633` |
| Product ID | `0x0009` |
| Interface Class | Vendor Specific (0xFF) |
| Endpoints | 0x01 OUT, 0x81 IN (init), 0x02 OUT, 0x82 IN (data) |
| Max Packet Size | 64 bytes |

## Packet Structure

All packets are 48 bytes with the following structure:

```
Offset  Size  Description
------  ----  -----------
0-1     2     Header: 0xAA 0x2E
2       1     Command byte
3-41    39    Payload (command-specific)
42-45   4     Footer: "HIDC" (0x48 0x49 0x44 0x43)
46-47   2     Checksum (little-endian sum of bytes 0-45)
```

### Response Packets

Device responses use header `0x55 0x2E` instead of `0xAA 0x2E`. The command byte is echoed back (or `0x00` for unknown commands).

## Endpoints

The device has two endpoint pairs:

- **Endpoint 0x01/0x81**: Used for initialization and configuration commands
- **Endpoint 0x02/0x82**: Used for display data updates

## Initialization Sequence

After a cold boot, the device shows a logo and ignores display data. The following sequence must be sent on **endpoint 0x01** to activate Machine Info mode:

### 1. Device Info Request (0x12)
```
AA 2E 12 00 00 ... 00 48 49 44 43 [checksum]
```
Response contains device serial number as ASCII string.

### 2. Configuration (0x02)
```
AA 2E 02 01 00 03 01 24 00 ... 00 48 49 44 43 [checksum]
```
Payload: `01 00 03 01 24`

### 3. Setup Commands (0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0B)
```
0x03: payload 01
0x04: payload 05 00 00 01
0x07: payload 00 02
0x08: payload 00 04
0x05: payload 01 01
0x0B: no payload
0x06: payload 01
```

### 4. Display Labels (0x15, 0x16, 0x17)
```
0x15: payload 2D 2D ("--")
0x16: payload 2D 2D ("--")
0x17: payload 2D 2D ("--")
```
These may set default display labels.

### 5. Mode Switch (0x0A)
```
AA 2E 0A EA 07 02 02 02 27 21 00 ... 00 48 49 44 43 [checksum]
```
Payload: `EA 07 02 02 02 27 21`

This command activates Machine Info display mode.

### 6. Status Request (0x10)
```
AA 2E 10 00 00 ... 00 48 49 44 43 00 02
```
Response byte 5 indicates current mode:
- `0xFF` = Machine Info mode (active)
- `0x00` = Image/GIF mode

## Display Data Command (0x01)

Sent on **endpoint 0x02** to update the display. This is the main command for showing system metrics.

### Packet Layout
```
Offset  Description              Example
------  -----------              -------
0-1     Header                   AA 2E
2       Command                  01
3       CPU Temperature (°C)     22 (34°C)
4-5     Reserved                 00 00
6       CPU Usage (%)            07 (7%)
7-8     Reserved                 00 00
9       RAM Usage (%)            04 (4%)
10      Reserved                 00
11      Flag                     09
12      Flag                     03
13      Reserved                 00
14      GPU Temperature (°C)     24 (36°C)
15      Flag                     05
16      Reserved                 00
17      Flag                     06 or 07
18      Flag                     0C
19      Reserved                 00
20      Flag                     07
21      GHz integer part         05 (5.xx GHz)
22      Reserved                 00
23      GHz decimal (tens)       20 (0.20)
24-25   MHz value (little-end)   50 14 (5200 MHz)
26      Reserved                 00
27-28   MHz value (repeated)     50 14
29-41   Reserved                 00 ...
42-45   Footer                   48 49 44 43
46-47   Checksum                 [calculated]
```

### Temperature-Based LED Color

The device has built-in temperature thresholds that change the LED ring color:
- Green: Low temperature
- Yellow/Orange: Medium temperature
- Red: High temperature (warning)

The exact thresholds are controlled by the device firmware.

## Status Request Command (0x10)

Query the current device state.

### Request
```
AA 2E 10 00 00 ... 00 48 49 44 43 00 02
```

### Response
```
55 2E 10 00 03 FF 00 ... 00 48 49 44 43 [checksum]
         ^^ ^^ ^^
         |  |  +-- Mode: FF=Machine Info, 00=Image
         |  +-- Unknown (always 03?)
         +-- Echo of command
```

## Checksum Calculation

The checksum is a simple sum of all bytes from offset 0 to 45, stored as a 16-bit little-endian value at offsets 46-47.

```c
uint16_t checksum = 0;
for (int i = 0; i < 46; i++) {
    checksum += packet[i];
}
packet[46] = checksum & 0xFF;
packet[47] = (checksum >> 8) & 0xFF;
```

## Example: Complete Initialization + Display Update

```c
// 1. Send init sequence on endpoint 0x01
send_packet(EP_0x01, build_packet(0x12, NULL));
send_packet(EP_0x01, build_packet(0x02, "\x01\x00\x03\x01\x24"));
send_packet(EP_0x01, build_packet(0x03, "\x01"));
send_packet(EP_0x01, build_packet(0x04, "\x05\x00\x00\x01"));
send_packet(EP_0x01, build_packet(0x07, "\x00\x02"));
send_packet(EP_0x01, build_packet(0x08, "\x00\x04"));
send_packet(EP_0x01, build_packet(0x05, "\x01\x01"));
send_packet(EP_0x01, build_packet(0x0B, NULL));
send_packet(EP_0x01, build_packet(0x06, "\x01"));
send_packet(EP_0x01, build_packet(0x15, "\x2d\x2d"));
send_packet(EP_0x01, build_packet(0x16, "\x2d\x2d"));
send_packet(EP_0x01, build_packet(0x17, "\x2d\x2d"));
send_packet(EP_0x01, build_packet(0x0A, "\xea\x07\x02\x02\x02\x27\x21"));

// 2. Send display data on endpoint 0x02
while (running) {
    send_packet(EP_0x02, build_packet(0x10, NULL));  // Status request
    recv_packet(EP_0x82);

    send_packet(EP_0x02, build_display_packet(cpu_temp, cpu_usage, gpu_temp, ram_usage, cpu_mhz));
    recv_packet(EP_0x82);

    sleep(1);
}
```

## Known Command Bytes

| Command | Endpoint | Description |
|---------|----------|-------------|
| 0x01 | 0x02 | Display data update |
| 0x02 | 0x01 | Configuration |
| 0x03 | 0x01 | Setup |
| 0x04 | 0x01 | Setup |
| 0x05 | 0x01 | Setup |
| 0x06 | 0x01 | Setup |
| 0x07 | 0x01 | Setup |
| 0x08 | 0x01 | Setup |
| 0x0A | 0x01 | Mode switch |
| 0x0B | 0x01 | Setup |
| 0x10 | 0x02 | Status request |
| 0x12 | 0x01 | Device info request |
| 0x15 | 0x01 | Display label |
| 0x16 | 0x01 | Display label |
| 0x17 | 0x01 | Display label |

## Notes

- The device must be initialized after every cold boot (full power cycle)
- Warm reboots may preserve the device state
- Images/GIFs uploaded via Windows software persist in device memory
- The protocol was reverse-engineered from USB captures of the Windows DeepCreative software

## References

- Implementation: `deepcooldevice.cpp`
- Protocol reverse-engineered from USB captures of Windows DeepCreative software
