# hypr-quiklight

A lightweight Linux/Wayland ambilight driver for **ROBOBLOQ / DX-LIGHT Quiklight** USB LED strips.

`hypr-quiklight` captures the edges of a Wayland output, computes average colors in real time, post-processes them for more vivid rendering, and sends them directly to the Quiklight HID controller.

This project was built to provide a native Linux alternative to the original vendor app, with a focus on:

- low latency
- vivid and configurable colors
- direct HID communication
- automatic device detection
- simple user-level installation
- clean integration with Wayland sessions

---

## Features

- Native **Wayland** screen-edge capture
- Direct **HID** communication with the Quiklight controller
- Automatic detection of the correct HID device
- Real-time ambilight rendering
- Configurable:
  - brightness
  - FPS limit
  - smoothing
  - saturation boost
  - value boost
  - gamma
  - minimum saturation
  - hue shift
  - strip mapping and segment orientation
- Random LED sequence mode for strip order calibration
- User service via `systemd --user`
- No Electron, no browser stack, no cloud dependency

---

## Supported hardware

This project is designed for the Quiklight controller identified as:

- **Vendor ID:** `0x1A86`
- **Product ID:** `0xFE07`
- Typically reported as:
  - manufacturer: `ROBOBLOQ`
  - product: `USBHID`

If your controller reports the same HID identity, it should be compatible.
More HID can work tough, change the vendor & product ID in code to test it.

---

## Wayland compatibility

This project targets **wlroots-based** compositors exposing the capture protocols needed by the project.

### Best supported
- **Hyprland**
- Other compositors exposing the same image capture stack

### May work depending on protocol support
- **Sway**
- Other wlroots-based desktops/compositors, if the required protocols are available

### Not supported or not expected to work
- **GNOME / Mutter**
- **KDE Plasma / KWin**
- X11-only environments

### Important note
Wayland capture support depends on the compositor, the enabled protocols, and sometimes the desktop portal stack.  
Even on Wayland, not all compositors expose the same capture APIs.

This project was primarily tuned and validated on **Hyprland**.

---

## How it works

`hypr-quiklight`:

1. connects to the Wayland compositor
2. captures a selected output
3. samples the screen edges
4. computes average colors for each logical LED zone
5. applies optional color enhancement:
   - saturation boost
   - brightness boost
   - gamma correction
   - hue shift
   - smoothing
6. remaps the logical zones to the physical strip layout
7. sends the final frame to the Quiklight HID controller

---

## Project layout

```text
.
├── CMakeLists.txt
├── README.md
├── protocols/
├── src/
├── wayland/
└── dist/
    ├── install.sh
    ├── uninstall.sh
    ├── hypr-quiklight.service
    ├── hypr-quiklight-launcher
    ├── 99-quiklight.rules
    └── config.ini.example
```

---

## Dependencies

Typical build dependencies on Arch-based systems:

- `cmake`
- `pkgconf`
- `hidapi`
- `wayland`
- `waylandpp`
- `wayland-protocols`
- `base-devel`

Example:

```bash
sudo pacman -S base-devel cmake pkgconf hidapi wayland waylandpp wayland-protocols
```

---

## Build

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

---

## Quick test before installation

List HID devices:

```bash
./hypr-quiklight --list-devices
```

Test the strip with a static random sequence:

```bash
./hypr-quiklight --random-sequence
```

Test live ambilight manually:

```bash
./hypr-quiklight -o yourmonitor
```

Or with an explicit HID path:

```bash
./hypr-quiklight --device /dev/hidraw0 -o yourmonitor
```

---

## Installation

A user-level installer is provided.

From the project root:

```bash
./dist/install.sh
```

Then enable the service:

```bash
systemctl --user daemon-reload
systemctl --user enable --now hypr-quiklight.service
```

---

## Uninstallation

```bash
./dist/uninstall.sh
```

If needed, stop and disable the user service manually:

```bash
systemctl --user stop hypr-quiklight.service
systemctl --user disable hypr-quiklight.service
```

---

## udev permissions

The Quiklight controller is a HID device and needs appropriate permissions.

This project installs a `udev` rule similar to:

```udev
KERNEL=="hidraw*", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="fe07", MODE="0666"
```

If the device is still not accessible after installation:

```bash
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Then unplug and replug the USB controller.

---

## Configuration

The user config file is located at:

```text
~/.config/hypr-quiklight/config.ini
```

Example (using Xiaomi Monitor) :

```ini
output=Xiaomi
brightness=255
fps=60
smoothing=0.35
hue_shift=0

saturation_boost=1.60
value_boost=1.14
gamma=0.93
min_saturation=0.18

reverse_top=1
reverse_right=1
reverse_left=0
swap_left_right=0
top_offset=0
right_offset=0
left_offset=0

idle_sleep_ms=0
verbose=0
```

---

## Configuration reference

### Output and device

- `output`  
  Substring used to match the Wayland output description.

- `device`  
  Optional explicit HID path such as `/dev/hidraw0`.  
  Usually not needed if automatic detection works.

- `brightness`  
  Integer from `0` to `255`.

---

### Timing and smoothing

- `fps`  
  Target maximum update rate.  
  Typical values:
  - `30`
  - `45`
  - `60`

- `smoothing`  
  Frame blending factor for temporal smoothing.  
  Typical range:
  - `0.0` = no smoothing
  - `0.2` to `0.4` = moderate smoothing
  - `0.5+` = very soft transitions

---

### Color controls

- `saturation_boost`  
  Multiplies saturation for a more vivid look.

- `value_boost`  
  Multiplies brightness/value.

- `gamma`  
  Adjusts perceived brightness, especially in midtones.

- `min_saturation`  
  Prevents colors from becoming too gray.

- `hue_shift`  
  Rotates hue in degrees.  
  Useful for correcting a persistent hue bias.

---

### Mapping controls

- `reverse_top`
- `reverse_right`
- `reverse_left`
- `swap_left_right`
- `top_offset`
- `right_offset`
- `left_offset`

These parameters control how the logical capture zones are mapped onto the physical strip.

They are useful if your LED strip orientation differs from the default mapping.

---

### Misc

- `idle_sleep_ms`  
  Optional sleep after each loop iteration.

- `verbose`  
  Set to `1` for debug logs.

---

## Recommended presets

### Natural
```ini
fps=60
smoothing=0.25
hue_shift=0
saturation_boost=1.30
value_boost=1.08
gamma=0.98
min_saturation=0.15
```

### Vibrant
```ini
fps=60
smoothing=0.35
hue_shift=0
saturation_boost=1.60
value_boost=1.14
gamma=0.93
min_saturation=0.18
```

### Strong gaming profile
```ini
fps=60
smoothing=0.35
hue_shift=0
saturation_boost=1.75
value_boost=1.18
gamma=0.90
min_saturation=0.20
```

---

## Calibration and strip order testing

To verify strip order and color correctness:

```bash
hypr-quiklight --random-sequence
```

This displays a static random LED pattern and prints the corresponding sequence in the terminal.

This mode is useful for:

- checking physical LED order
- validating segment direction
- confirming color correctness
- tuning mapping settings

---

## Running manually

You can run the program without the service:

```bash
hypr-quiklight -o yourmonitor
```

With explicit device path:

```bash
hypr-quiklight --device /dev/hidraw0 -o yourmonitor
```

With custom color tuning:

```bash
hypr-quiklight -o yourmonitor \
  --saturation-boost 1.70 \
  --value-boost 1.18 \
  --gamma 0.90 \
  --hue-shift 0 \
  --fps 60 \
  --smoothing 0.35
```

---

## Troubleshooting

### The strip is detected but cannot be opened
Check permissions on the HID device:

```bash
ls -l /dev/hidraw*
```

Make sure the `udev` rule is installed and reloaded.

---

### The HID path changes after reconnecting
That is normal on Linux.  
Use automatic detection instead of hardcoding `/dev/hidrawX` whenever possible.

---

### Colors look washed out
Increase:

- `saturation_boost`
- `value_boost`

Try:

```ini
saturation_boost=1.70
value_boost=1.16
gamma=0.92
```

---

### Colors are too aggressive or unstable
Reduce:

- `saturation_boost`
- `value_boost`

Increase:

- `smoothing`

Try:

```ini
smoothing=0.50
saturation_boost=1.35
value_boost=1.08
gamma=0.97
```

---

### Hue is globally wrong
Use `hue_shift` to rotate colors slightly.

Try values like:

- `-10`
- `-5`
- `5`
- `10`

---

### LED order is wrong
Use:

- `reverse_top`
- `reverse_right`
- `reverse_left`
- offsets

Also use `--random-sequence` to validate the physical mapping.

---

### Flickering after a compositor crash
If the compositor or shell crashes, stop the service, unplug/replug the HID controller if necessary, then restart the service:

```bash
systemctl --user stop hypr-quiklight.service
pkill -f hypr-quiklight
systemctl --user start hypr-quiklight.service
```

---

## Known limitations

- This project depends on compositor-specific Wayland capture support.
- It is not a universal Wayland ambilight solution for all desktops.
- Compatibility is best on compositors exposing the required capture protocols.
- HID behavior may vary slightly across Quiklight firmware revisions.

---

## Safety note

This project writes continuously to a USB HID LED controller.  
If you experience instability:

- lower `fps`
- increase `smoothing`
- reduce color aggressiveness
- restart the service after compositor crashes

---

## Why this project exists

The original Quiklight software is built around an Electron-based desktop application and is not a clean native Linux solution.

`hypr-quiklight` provides:

- a native Linux implementation
- direct Wayland capture
- direct HID output
- simple deployment
- no unnecessary desktop stack

---

## License

- `GPL-3.0`

---

## Credits

- Original hardware and protocol inspiration: Quiklight / ROBOBLOQ ecosystem
- Wayland capture work built around Linux Wayland protocol tooling
- Linux HID communication via `hidapi`

---

## Status

Usable and actively tuned.  
Best results currently expected on Hyprland-based Wayland setups.
