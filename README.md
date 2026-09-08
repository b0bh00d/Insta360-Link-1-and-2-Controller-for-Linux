# Insta360 Link Webcam Controller for Linux — C++ / Qt port

This is an AI-generated C++/Qt port of the [Free Pascal + Lazarus](https://github.com/vrwallace/Insta360-Link-1-and-2-Controller-for-Linux) application created by vrwallace. It controls
the **Insta360 Link** and **Insta360 Link 2** webcams on Linux via V4L2 standard
controls and UVC Extension Unit (XU) commands.

Two programs are built:

| Target | Source dir | Description |
|--------|-----------|-------------|
| `insta360linkgui` | `app/` | Qt Widgets GUI: live preview, press-and-hold PTZ, sliders, mode buttons, presets, activity log |
| `linkctl` | `cli/` | Command-line controller for scripting/automation |

Shared V4L2 + camera-controller code lives in `src/core/` and is compiled into
both targets via `src/core/core.pri`.

## Prerequisites

- Linux with V4L2 / UVC (any recent kernel)
- **Qt 5.15+** *or* **Qt 6.2+** development packages, plus `qmake` and `g++`

  ```bash
  # Debian/Ubuntu — Qt 5
  sudo apt install qtbase5-dev qtbase5-dev-tools g++ make

  # Debian/Ubuntu — Qt 6
  sudo apt install qt6-base-dev qt6-base-dev-tools libgl-dev g++ make
  ```

The `.pro` files are Qt5/Qt6-agnostic; `qmake` builds against whichever Qt is on
`PATH` (`qmake6` for Qt 6). This tree was developed and verified against Qt 5.15.

## Building

```bash
cd Insta360-Link-1-and-2-Controller-for-Linux
qmake            # or: qmake6
make -j$(nproc)
```

Binaries land in `app/insta360linkgui` and `cli/linkctl`.
`make install` copies both to `/usr/local/bin` (override with `INSTALL_ROOT`).

## Device permissions

By default V4L2 nodes need root. To avoid `sudo`:

```bash
sudo cp 99-insta360-link.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
# or: sudo usermod -aG video $USER   (re-login afterwards)
```

## Usage

### GUI

```bash
./app/insta360linkgui
```

Select the camera, click **Connect** (live preview auto-starts). **Hold** a D-pad
button to move the gimbal continuously; the Pan/Tilt step spin-boxes set the
speed. Settings and presets persist to
`~/.config/insta360linkgui/insta360link.ini`.

### CLI

```bash
./cli/linkctl list                 # enumerate video devices
./cli/linkctl -d /dev/video0 info  # camera info + all controls
./cli/linkctl move 10 0            # relative pan right
./cli/linkctl zoom 200             # 2x zoom
./cli/linkctl tracking on          # AI tracking
./cli/linkctl frame half           # framing: head|half|full
./cli/linkctl deskview on          # deskview | whiteboard | overhead | normal
./cli/linkctl wb 5600              # manual white balance (Kelvin)
./cli/linkctl preset save 0        # save/recall preset slots 0..5
./cli/linkctl -v xu 3 01           # raw XU selector write (hex bytes)
```

`-v` prints the XU probe / scan log. Run `linkctl help` for the full command
list.

## Notes on the port

- Uses the kernel UAPI headers (`<linux/videodev2.h>`, `<linux/uvcvideo.h>`)
  directly instead of hand-rolled ioctl structs.
- The Pascal `OnLog` event is a Qt signal, `Insta360Link::logMessage(QString)`.
- GUI is built in code with Qt layouts (resizable / HiDPI-safe) rather than
  absolute pixel coordinates; press-and-hold PTZ uses `QAbstractButton`
  `pressed()`/`released()`.
- Preview frames decode to `QImage` (`loadFromData` for MJPEG; the same integer
  YUYV→RGB math as the original for the fallback path).
- Settings use `QSettings` (INI) with the same sections/keys as the Lazarus
  version; `linkctl` returns a non-zero exit code when a command fails.
- Camera-model detection maps USB PID `4c01`→Link, `4c04`→Link 2 (unchanged from
  the original); newer variants such as the Link 2 Pro (`4c06`) report as
  "Unknown" but function normally.
