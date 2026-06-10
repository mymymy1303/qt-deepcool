# Reverse-engineering the MYSTIQUE USB protocol

A self-contained method and toolkit for reverse-engineering the USB protocol
of DeepCool LCD coolers (VID 0x3633): setting up the capture environment,
the general shape of the protocol frames, and tools to decode captures,
replay them and test hypotheses on the device. Nothing in the method is tied
to one feature; the same loop applies to display modes, GIF/video, the
unknown setup commands, or another DeepCool PID. It was built and
battle-tested while cracking the image upload, and that investigation is kept
at the end as a worked example.

There is no public documentation for these devices. The ground truth is the
USB traffic of the official Windows app, **DeepCreative**, captured with
`usbmon` while the app drives the real cooler passed through to a Windows VM.

The loop is:

1. **Capture** what DeepCreative sends when you use the feature you care about.
2. **Decode** the capture into commands and frames.
3. **Replay** the captured bytes from Linux to prove the transport, then
   generate your own frames and test hypotheses one at a time.
4. **Look at the LCD.** No tool can do this step: a human confirms what the
   screen shows. Keep that human in the loop and make every test count;
   don't guess-and-spam the device.

This guide is written for a human or an AI agent doing the work on a Linux
host. Sections marked 🧑 need a human (GUI interaction or looking at the
screen).

## Host setup

Requirements: Linux with hardware virtualization (`/dev/kvm`), ~10 GB free
disk, the cooler plugged in. You also need two files that licensing prevents
shipping here:

- a **Windows 10/11 ISO** (the free LTSC Evaluation ISO works),
- the **DeepCreative installer** from DeepCool's website.

Variables used throughout (adjust to taste):

```bash
DC_VID=3633                  # DeepCool vendor id
DC_PID=0009                  # 0009=MYSTIQUE, 000a=LD, 000c=LP, ...
DC_VM=deepcool-win           # libvirt domain name (qemu:///session)
DC_WORK=$HOME/deepcool-re    # VM disk + ISOs + captures live here
```

### Packages

```bash
# Arch
sudo pacman -S --needed qemu-full libvirt virt-install spice-gtk \
    wireshark-cli xorriso libjpeg-turbo imagemagick python python-pip
# Debian/Ubuntu
sudo apt install qemu-kvm libvirt-daemon-system virtinst spice-client-gtk \
    wireshark tshark xorriso libjpeg-turbo-progs imagemagick python3-venv
# Fedora
sudo dnf install qemu-kvm libvirt virt-install spice-gtk-tools \
    wireshark-cli xorriso libjpeg-turbo-utils ImageMagick python3

sudo modprobe usbmon                  # kernel side of USB capture
sudo usermod -aG libvirt $USER        # log out/in afterwards

python3 -m venv $DC_WORK/venv
$DC_WORK/venv/bin/pip install pyusb pillow
```

### Non-root access to the device and the capture interface

Session-mode QEMU needs rw on the USB node to pass the device through, pyusb
needs it to talk to the device, and `dumpcap` needs to read the usbmon node:

```bash
sudo tee /etc/udev/rules.d/99-deepcool-access.rules >/dev/null <<EOF
SUBSYSTEM=="usb", ATTR{idVendor}=="3633", MODE="0666", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="3633", MODE="0666", TAG+="uaccess"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger --attr-match=idVendor=3633

# find which bus the cooler is on: "Bus 001 ..." -> usbmon1
lsusb -d 3633:

sudo setcap cap_net_raw,cap_net_admin+eip "$(command -v dumpcap)"
sudo chmod o+r /dev/usbmon1   # dumpcap drops cap_dac_override, so this is needed
```

Re-run the `chmod` after a reboot. Everything in this section is reverted in
[Teardown](#teardown).

## The Windows VM

```bash
qemu-img create -f qcow2 $DC_WORK/$DC_VM.qcow2 40G
virt-install --connect qemu:///session \
  --name $DC_VM --memory 8192 --vcpus 4 --cpu host-passthrough \
  --disk path=$DC_WORK/$DC_VM.qcow2,bus=sata,format=qcow2 \
  --cdrom $DC_WORK/windows.iso --os-variant win10 \
  --network user,model=e1000e \
  --graphics spice,listen=127.0.0.1,port=5930 --video qxl \
  --channel spicevmc --controller usb,model=qemu-xhci \
  --redirdev usb,type=spicevmc --redirdev usb,type=spicevmc \
  --noautoconsole
# virt-install may leave the boot order as hd-only -> "No bootable device"
virt-xml --connect qemu:///session $DC_VM --edit --boot cdrom,hd
virsh --connect qemu:///session start $DC_VM
spicy -h 127.0.0.1 -p 5930 &      # or virt-viewer
```

Choices that avoid known traps: SATA disk and e1000e NIC mean Windows installs
without virtio drivers; SPICE is pinned to `127.0.0.1:5930` because
`listen=none` (the default) exposes no TCP port for `spicy`; `--boot bios` is
invalid (BIOS already is the default); `redirdev` must not specify `bus=usb.0`
with qemu-xhci.

🧑 Install Windows in the SPICE window (local offline account). After the
first reboot do *not* press a key at the "boot from CD" prompt.

To get the DeepCreative installer (or any file) into the VM, wrap it in an ISO
and hot-swap the CD:

```bash
mkdir -p $DC_WORK/_iso && cp DeepCreative-setup.exe $DC_WORK/_iso/
xorriso -as mkisofs -V DEEPCOOL -o $DC_WORK/payload.iso $DC_WORK/_iso
virsh --connect qemu:///session change-media $DC_VM sdb $DC_WORK/payload.iso --update --live
```

## USB passthrough

The cooler is owned by the host **or** by the VM, never both. Decoding
captures needs it in the VM; pyusb tests need it on the host.

```bash
cat > $DC_WORK/hostdev.xml <<EOF
<hostdev mode='subsystem' type='usb' managed='yes'>
  <source><vendor id='0x3633'/><product id='0x0009'/></source>
</hostdev>
EOF
virsh --connect qemu:///session attach-device $DC_VM $DC_WORK/hostdev.xml --live
virsh --connect qemu:///session detach-device $DC_VM $DC_WORK/hostdev.xml --live
```

🧑 DeepCreative usually shows "device not found" on the first attach. The fix
that works: **detach, wait 3 s, re-attach while the app is running** (the
WinUSB interface is single-owner; the re-attach forces a clean re-enumeration).

Passthrough keeps the device on the same *host* bus, so `usbmon` still sees
all its traffic. That is the whole trick: Windows drives the device, Linux
records it.

## Capturing

```bash
dumpcap -i usbmon1 -w $DC_WORK/capture.pcapng -q &
# 🧑 ... use the feature in DeepCreative (see notes below) ...
kill -INT %1
```

While capturing, exercise **only** the feature you are reverse-engineering,
and note exactly what you did and in which order. Expect feature-specific
quirks in DeepCreative itself (example from the image work: its importer
rejects PNGs with alpha, error 10003, and synthetic solid-color JPEGs,
error 10008, so real photos had to be used).

Capture **at least two samples** of the same operation with different inputs.
One sample lets you guess what a header field means; two or more let you prove
it (see the worked example below).

## Decoding

Device structure (from sysfs): one vendor-specific (class 0xFF) interface with
four bulk endpoints, `0x01`/`0x02` OUT and `0x81`/`0x82` IN. All control
packets are 48-byte "HIDC" frames: `AA 2E <cmd> <payload...> "HIDC"
<sum16 LE>`; device responses echo the command with a `55 2E` lead-in. Image
data is a "DCLd" frame sent in 64-byte bulk chunks (header layout in the
upload script below).

### Timeline of commands (the discovery tool)

The script below prints every HIDC command/response chronologically and
collapses the app's heartbeat (it re-sends the whole init sequence about once
per second, plus a clock on EP `0x02`). Diff a capture where you did nothing
against one where you clicked the feature once: the commands that only appear
in the second one are your protocol. This is how the gallery commands
(`0x08` select, `0x09` delete) and the `dcldfinish` trailer were found.

```python
#!/usr/bin/env python3
"""Chronological timeline of HIDC commands/responses in a usbmon capture.
Usage: timeline.py <capture.pcapng> [--all]"""
import binascii, subprocess, sys

def rows(pcap):
    out = subprocess.check_output([
        "tshark", "-r", pcap, "-Y", "usb.transfer_type==0x03 && usb.capdata",
        "-T", "fields", "-e", "frame.time_relative",
        "-e", "usb.endpoint_address", "-e", "usb.capdata"], text=True)
    res = []
    for line in out.splitlines():
        f = line.split("\t")
        if len(f) == 3 and f[2]:
            res.append((float(f[0]), int(f[1], 16), binascii.unhexlify(f[2])))
    return res

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    collapse = "--all" not in sys.argv
    last_seen, payloads, skipped = {}, {}, 0
    for t, ep, d in rows(sys.argv[1]):
        line = None
        if d[:4] == b"DCLd":
            line = (f"DCLd frame header, payload "
                    f"{int.from_bytes(d[5:8], 'little')} B, type 0x{d[4]:02x}")
        elif d[:10] == b"dcldfinish":
            line = "dcldfinish commit trailer"
        elif len(d) == 48 and d[0] in (0xAA, 0x55) and d[1] == 0x2E:
            sent = d[0] == 0xAA
            cmd_key, payload = (ep, sent, d[2]), bytes(d[3:16])
            seen = payloads.setdefault(cmd_key, set())
            seen.add(payload)
            # clocks/counters change payload every packet: once a command has
            # shown >3 distinct payloads, collapse it like a constant one
            key = cmd_key if len(seen) > 3 else cmd_key + (payload,)
            if collapse and t - last_seen.get(key, -10) < 5:
                last_seen[key] = t
                skipped += 1
                continue
            last_seen[key] = t
            line = (f"{'>' if sent else '<'} {'cmd' if sent else 'resp'} "
                    f"0x{d[2]:02x} payload {payload.hex(' ')}")
        elif len(d) != 64:  # 64-byte packets are image chunks, too noisy
            line = f"? data({len(d)}) {d[:24].hex(' ')}"
        if line:
            print(f"[{t:9.3f}] ep{ep:02x} {line}")
    if skipped:
        print(f"\n({skipped} heartbeat/clock packets collapsed; --all shows them.)")

if __name__ == "__main__":
    main()
```

### Carving bulk payloads

When the feature moves bulk data, reassemble the payload from the 64-byte
chunks, validate it, and diff the headers across samples so content-dependent
fields are proven instead of guessed. The script below does this for the only
bulk format known so far (DCLd image frames); adapt the magic, the header
layout and the validity check for whatever your capture contains:

```python
#!/usr/bin/env python3
"""Decode DCLd image uploads from a usbmon capture.
Usage: extract_frames.py <capture.pcapng>"""
import binascii, hashlib, os, struct, subprocess, sys

def tshark_rows(pcap):
    out = subprocess.check_output([
        "tshark", "-r", pcap,
        "-Y", "usb.endpoint_address==0x01 && usb.urb_type==0x53 && usb.capdata",
        "-T", "fields", "-e", "frame.time_relative", "-e", "usb.device_address",
        "-e", "usb.data_len", "-e", "usb.capdata"], text=True)
    rows = []
    for line in out.splitlines():
        f = line.split("\t")
        if len(f) == 4 and f[3]:
            rows.append((float(f[0]), f[1], int(f[2]), binascii.unhexlify(f[3])))
    return rows

def reassemble(rows):
    """The app's monitoring heartbeat (48-byte HIDC packets) shares EP 0x01
    and can interleave with the chunks of an aborted upload: skip those, and
    stop at the next DCLd header or the dcldfinish trailer."""
    frames, i = [], 0
    while i < len(rows):
        if rows[i][3][:4] == b"DCLd":
            hdr = rows[i][3]
            length = int.from_bytes(hdr[5:8], "little")
            payload, j = b"", i + 1
            while j < len(rows) and len(payload) < length:
                chunk = rows[j][3]
                if chunk[:4] == b"DCLd" or chunk[:10] == b"dcldfinish":
                    break
                if len(chunk) == 48 and chunk[0] == 0xAA and chunk[1] == 0x2E:
                    j += 1
                    continue
                payload += chunk
                j += 1
            frames.append((rows[i][0], hdr, payload[:length]))
            i = j
        else:
            i += 1
    return frames

def decode_header(hdr, jpeg):
    fields = {
        "magic": hdr[0:4].decode("latin1"),
        "type": hdr[4],
        "len_LE24[5:8]": int.from_bytes(hdr[5:8], "little"),
        "field[9:11]_LE16": struct.unpack("<H", hdr[9:11])[0],
        "id_hex[20:52]": hdr[20:52].decode("latin1", "replace"),
        "hdr_cksum[62:64]": struct.unpack("<H", hdr[62:64])[0],
    }
    checks = {
        "len == jpeg size": fields["len_LE24[5:8]"] == len(jpeg),
        "[9:11] == sum(jpeg)&0xFFFF": fields["field[9:11]_LE16"] == (sum(jpeg) & 0xFFFF),
        "[62:64] == sum(hdr[0:62])": fields["hdr_cksum[62:64]"] == (sum(hdr[:62]) & 0xFFFF),
        "id == md5(jpeg)": fields["id_hex[20:52]"] == hashlib.md5(jpeg).hexdigest(),
    }
    return fields, checks

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    pcap = sys.argv[1]
    base = os.path.splitext(pcap)[0]
    frames = reassemble(tshark_rows(pcap))
    if not frames:
        sys.exit("No DCLd image uploads found.")
    all_fields = []
    for n, (t, hdr, jpeg) in enumerate(frames, 1):
        open(f"{base}_img{n}.jpg", "wb").write(jpeg)
        open(f"{base}_frame{n}.bin", "wb").write(hdr + jpeg)
        fields, checks = decode_header(hdr, jpeg)
        all_fields.append(fields)
        valid = jpeg[:3] == b"\xff\xd8\xff" and jpeg[-2:] == b"\xff\xd9"
        print(f"--- upload #{n} @ t={t:.3f}s ({len(jpeg)} B JPEG, "
              f"{'valid' if valid else 'TRUNCATED/INVALID'}) ---")
        for k, v in fields.items():
            print(f"      {k:22} = {v}")
        for k, ok in checks.items():
            print(f"      [{'OK ' if ok else 'NO '}] {k}")
    if len(frames) >= 2:
        print("=== header diff (varying fields are content-dependent) ===")
        for k in all_fields[0]:
            vals = [f[k] for f in all_fields]
            tag = "VARIES" if len(set(map(str, vals))) > 1 else "const"
            print(f"  {tag:6} {k}: {vals}")

if __name__ == "__main__":
    main()
```

## Replaying and testing on the real device

Detach the cooler from the VM first (see above), then drive it with pyusb.

### Send a single command

Once the timeline suggests what a command does, send it alone and watch the
response and the LCD. This proved slot selection (`0x08 00 <n>`), slot
deletion (`0x09`), and distinguished a counter from an echo in the responses.

```python
#!/usr/bin/env python3
"""Send one HIDC command to the cooler and print the response.
Usage: hidc.py <cmd> [payload-hex] [--ep2]
  hidc.py 0x10            query on EP 0x01 / read 0x81
  hidc.py 0x08 0004       cmd 0x08 payload 00 04 (select gallery slot 4)
Only send commands you have seen in a capture, with captured payload shapes.
Do not fuzz unknown commands blindly against the only device you own."""
import sys
import usb.core, usb.util

VID, PID = 0x3633, 0x0009

def hidc(cmd, payload=b""):
    p = bytearray(48)
    p[0], p[1], p[2] = 0xAA, 0x2E, cmd
    p[3:3 + len(payload)] = payload[:39]
    p[42:46] = b"HIDC"
    cs = sum(p[:46]) & 0xFFFF
    p[46], p[47] = cs & 0xFF, (cs >> 8) & 0xFF
    return bytes(p)

def main():
    args = [a for a in sys.argv[1:] if a != "--ep2"]
    ep_out, ep_in = (0x02, 0x82) if "--ep2" in sys.argv else (0x01, 0x81)
    if not args:
        sys.exit(__doc__)
    cmd = int(args[0], 16)
    payload = bytes.fromhex(args[1]) if len(args) > 1 else b""
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("device not found (still attached to the VM? udev rule?)")
    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except Exception:
        pass
    try:
        dev.set_configuration()
    except Exception:
        pass
    usb.util.claim_interface(dev, 0)
    pkt = hidc(cmd, payload)
    print(f"> ep{ep_out:02x} {pkt[:16].hex(' ')} ...")
    dev.write(ep_out, pkt, timeout=1000)
    try:
        r = bytes(dev.read(ep_in, 64, timeout=1000))
        print(f"< ep{ep_in:02x} {r[:24].hex(' ')}")
    except usb.core.USBError as e:
        print(f"< no response ({e})")
    usb.util.release_interface(dev, 0)

if __name__ == "__main__":
    main()
```

### Replay a captured frame / drive a finished feature

What a fully reverse-engineered feature looks like as a standalone driver,
using the image upload as the example. `--raw-frame` replays an exact
captured frame,
which proves the transport with zero unknowns; that was the first thing that
ever put an image on the LCD from Linux.

```python
#!/usr/bin/env python3
"""Upload an image to the MYSTIQUE LCD (init -> 0x0F -> DCLd+JPEG -> dcldfinish
-> select slot). Usage:
  upload.py <image>              encode, upload, display (slot 0)
  upload.py <image> --clear      empty the device gallery first (deterministic)
  upload.py <image> --slot N     select slot N instead of 0
  upload.py --raw-frame <f.bin>  replay an exact captured DCLd frame"""
import io, struct, sys, time
import usb.core, usb.util

VID, PID = 0x3633, 0x0009
EP_OUT, EP_IN = 0x01, 0x81
W, H = 480, 640  # portrait LCD

def hidc(cmd, payload=b""):
    p = bytearray(48)
    p[0], p[1], p[2] = 0xAA, 0x2E, cmd
    p[3:3 + len(payload)] = payload[:39]
    p[42:46] = b"HIDC"
    cs = sum(p[:46]) & 0xFFFF
    p[46], p[47] = cs & 0xFF, (cs >> 8) & 0xFF
    return bytes(p)

INIT = [
    hidc(0x12), hidc(0x02, bytes.fromhex("0101000024")), hidc(0x03, b"\x02"),
    hidc(0x04), hidc(0x07), hidc(0x08), hidc(0x05, b"\x01\x01"), hidc(0x0B),
    hidc(0x06, b"\x01"), hidc(0x15, b"qt-deepcool"), hidc(0x16, b"--"),
    hidc(0x17, b"--"),
]

def encode_jpeg(path):
    from PIL import Image
    im = Image.open(path).convert("RGB")
    scale = max(W / im.width, H / im.height)
    im = im.resize((round(im.width * scale), round(im.height * scale)),
                   Image.LANCZOS)
    l, t = (im.width - W) // 2, (im.height - H) // 2
    im = im.crop((l, t, l + W, t + H))
    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=95, subsampling="4:2:0")
    return buf.getvalue()

def build_frame(jpeg):
    import hashlib
    h = bytearray(64)
    h[0:4] = b"DCLd"
    h[4] = 0x01                                         # type: still image
    h[5:8] = len(jpeg).to_bytes(3, "little")
    h[9:11] = struct.pack("<H", sum(jpeg) & 0xFFFF)     # content sum16 (device seems to ignore it; set it right anyway)
    h[20:52] = hashlib.md5(jpeg).hexdigest().encode()   # any 32 hex chars work
    cs = sum(h[:62]) & 0xFFFF
    h[62], h[63] = cs & 0xFF, (cs >> 8) & 0xFF
    return bytes(h) + jpeg

def open_device():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        sys.exit("device not found (still attached to the VM? udev rule?)")
    try:
        if dev.is_kernel_driver_active(0):
            dev.detach_kernel_driver(0)
    except Exception:
        pass
    try:
        dev.set_configuration()
    except Exception:
        pass
    usb.util.claim_interface(dev, 0)
    return dev

def send_init(dev):
    for pkt in INIT:
        dev.write(EP_OUT, pkt, timeout=1000)
        try: dev.read(EP_IN, 64, timeout=300)
        except usb.core.USBError: pass
        time.sleep(0.005)

def select_slot(dev, slot):
    """cmd 0x08, payload is TWO bytes: 00 <slot> (0-based)."""
    dev.write(EP_OUT, hidc(0x08, bytes([0x00, slot])), timeout=1000)
    try: dev.read(EP_IN, 64, timeout=300)
    except usb.core.USBError: pass

def clear_gallery(dev, max_images=8):
    """0x09 deletes the SELECTED slot; on an empty gallery it is a no-op,
    so select-slot-0 + delete, repeated, empties any gallery."""
    for _ in range(max_images):
        select_slot(dev, 0)
        dev.write(EP_OUT, hidc(0x09), timeout=1000)
        try: dev.read(EP_IN, 64, timeout=300)
        except usb.core.USBError: pass
        time.sleep(0.1)

def arm_transfer(dev):
    """cmd 0x0F. The 55 2E 0F ack is mandatory: if a heartbeat response comes
    back instead, the device is out of sync and the upload would be lost."""
    for _ in range(2):
        dev.write(EP_OUT, hidc(0x0F), timeout=1000)
        try:
            resp = bytes(dev.read(EP_IN, 64, timeout=1000))
            if len(resp) >= 3 and resp[2] == 0x0F:
                return
            print(f"no 0x0F ack (got {resp[:4].hex()}), re-initializing...")
        except usb.core.USBError:
            print("no 0x0F response, re-initializing...")
        send_init(dev)
    sys.exit("device never acked 0x0F — aborting instead of uploading blind")

def upload(dev, frame, slot=0, clear=False):
    send_init(dev)
    if clear:
        clear_gallery(dev)
    arm_transfer(dev)
    for i in range(0, len(frame), 64):
        dev.write(EP_OUT, frame[i:i + 64], timeout=2000)
    dev.write(EP_OUT, b"dcldfinish" + b"\x00" * 45, timeout=2000)  # commit!
    time.sleep(0.2)
    select_slot(dev, slot)

def main():
    args = sys.argv[1:]
    slot, clear = 0, "--clear" in args
    if clear:
        args.remove("--clear")
    if "--slot" in args:
        i = args.index("--slot"); slot = int(args[i + 1]); del args[i:i + 2]
    if args[:1] == ["--raw-frame"]:
        frame = open(args[1], "rb").read()
    else:
        frame = build_frame(encode_jpeg(args[0]))
    dev = open_device()
    upload(dev, frame, slot, clear)
    usb.util.release_interface(dev, 0)
    print("done — check the LCD.")

if __name__ == "__main__":
    main()
```

## Worked example: how the image upload was cracked

The sequence of findings, kept here because the *mistakes* are the useful part:

1. Static analysis of DeepCreative dead-ended fast: the protocol logic is V8
   bytecode (`index.jsc`, bytenode) with no source stored. Only the strings
   were readable; they revealed JPEG conversion (`convertImgToJpeg`) and the
   IPC handler names, which told us *what* to look for, not the bytes.
   Capture was the productive route.
2. A first capture gave the transport: init sequence, `0x0F` arm, DCLd header,
   64-byte chunking. Replaying the exact captured bytes displayed the captured
   image, so the transport was proven.
3. Every *generated* frame then failed silently (no USB error, screen
   unchanged). Weeks of plausible-but-wrong hypotheses followed: JPEG
   quantization tables (byte-identical to the accepted file, so no), a content
   checksum in the header (computed correctly, still "rejected"), size limits
   (no). The actual cause was none of these.
4. A second capture with two photo uploads supplied the missing pieces in one
   sitting: a 55-byte `dcldfinish` trailer after the chunks (the commit;
   frames without it are stored but never shown, which *looks like* a
   rejection), the `0x08 00 <n>` slot-select and `0x09` slot-delete commands,
   and proof that the header checksum at [9:11] is `sum(jpeg) & 0xFFFF`
   (it matched on all three samples). It also showed the device keeps a
   persistent gallery, which had been silently accumulating our "rejected"
   test images all along, shifting what each slot displayed.

The lessons, in the order they were paid for:

- **Two captures beat ten hypotheses.** Every field meaning that survived was
  proven by diffing samples, not deduced from one.
- **A "rejected" frame may be an accepted, uncommitted frame.** When the
  screen shows nothing (or the wrong image), suspect device *state* — pending
  uploads, gallery slots — before suspecting your bytes.
- **Replay-the-exact-capture is the control experiment.** It splits the
  problem in half: transport vs. content.
- **Watch for the ack.** If `0x0F` is not acknowledged with `55 2E 0F`, the
  device is out of sync and the upload is lost (DeepCreative itself hits
  this and shows an upload error). Re-init and retry; don't send blind.
- **The heartbeat pollutes captures.** The app re-sends its init/monitoring
  packets about once per second on the same endpoints; collapse them when
  reading captures and skip them when reassembling frames.

## Open leads

- **GIF/video**: DeepCreative converts GIFs to... something. The DCLd header
  type byte is `0x01` for still images; capture a GIF upload and check the
  type and framing. Strings in the app (`convertGifToJpeg`,
  `update-motion-mode-screen`) suggest motion modes exist.
- The init commands `0x04`, `0x05`, `0x06`, `0x07`, `0x0B` are sent with
  fixed payloads and their meaning is unknown. Same for most response fields.
- There is no known command to query the gallery size; finding one would
  remove the clear-before-upload workaround.

## Teardown

```bash
virsh --connect qemu:///session destroy $DC_VM
virsh --connect qemu:///session undefine $DC_VM
rm -rf $DC_WORK                                        # disk, ISOs, captures

# revert the host permission changes
sudo rm /etc/udev/rules.d/99-deepcool-access.rules
sudo udevadm control --reload-rules
sudo setcap -r "$(command -v dumpcap)"
# the /dev/usbmonN chmod does not survive a reboot
```
