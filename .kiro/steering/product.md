# DeepCool Digital Controller

Linux application for controlling DeepCool digital cooling devices (AIO coolers, air coolers, cases with digital displays).

## Purpose
- Display real-time system metrics (CPU/GPU temperature, usage, RAM) on DeepCool device screens
- Support both GUI (Qt Widgets) and headless CLI operation for servers
- Provide Linux-native alternative to Windows-only DeepCool software

## Target Devices
- DeepCool MYSTIQUE 240/360 (primary target, VID: 0x3633, PID: 0x0009)
- Various DeepCool DIGITAL series: AK400/500/620, LS520/720, CH510/560, etc.
- Devices communicate via HID or vendor-specific USB protocols

## Current State
- Device detection and connection working
- Protocol partially reverse-engineered (48-byte packets with header/footer/checksum)
- GUI and CLI applications functional but protocol implementation incomplete
