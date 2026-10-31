# Corinth PTZ

Desktop ground-station app for PTZ and gimbal cameras on the Atlantis USV.
Qt 6 + QML front end, C++ core. Runs on Windows (primary), Linux, and can be
built for Android (UniRC 7 Pro) later from the same code.

It opens with a **Demo camera** so every screen works without hardware.

## What is in this build

| Area | Status |
|---|---|
| Live view: letterboxed feed, collapsible panels, toolbar, status bar | Working |
| Day / Dark / Night (red) themes | Working |
| PTZ: pad, on-screen joystick, keyboard (WASD/arrows, Q/E, Space, 1–9, T, N) | Working |
| Zoom / focus / iris hold-to-move, one-push focus, home, presets (go, set, rename) | Working (ISAPI), partly (ONVIF, see below) |
| Safety: stop on release / focus loss / link loss / control release, 250 ms resend, speed cap 30 when degraded | Working, covered by self-test |
| Link health: RTT, jitter, loss, auto sub-stream, lost-link lock, simulate switch | Working |
| Command acknowledgement bar and audit log (on screen, daily CSV, export) | Working |
| Control token (Take control / Release, view-only lock) | Working (single laptop) |
| Snapshots with SHA-256 and JSON metadata sidecar | Working |
| Recording (ffmpeg stream copy, segmented .mkv) | Working if ffmpeg is installed |
| Physical joystick: Xbox-type via XInput; flight sticks, PTZ keyboards, generic HID via SDL2 | Working; per-type bindings, deadband, expo, invert |
| Settings: 27 pages in the Hikvision layout, change tracking, Apply/Discard, re-read after write | Working; **wired fields** listed below |
| Camera discovery (ONVIF WS-Discovery) with "New camera found" pop-up, Display / Edit URL | Working on the local network segment |
| Camera drivers: ISAPI (Hikvision/OEM), ONVIF Profile S, RTSP-only, Demo | Working |

### Settings fields wired to the camera (ISAPI)

Device name, model, serial, firmware · NTP sync mode, server, interval · reboot,
restore, factory reset · DHCP, IP, mask · main and sub stream resolution, codec,
frame rate, bitrate type, max bitrate, I-frame interval · brightness, contrast,
saturation, sharpness, day/night, white balance, WDR · SD card capacity, free,
status, format · home position set/go/clear.

Everything else on the Settings pages is laid out and tracked but marked
**"Not mapped to the camera yet"**. Applying those keeps the value locally and
logs it as not sent. To wire one, see *Confirming endpoints* below and add a
`src` to that field in `qml/js/Schema.js`.

### Known gaps (honest list)

- **Not yet compiled on Windows by me.** It builds and passes its self-test on
  Linux with Qt 6.4 (GCC 13). Windows-specific code paths (XInput, windeployqt
  step, MSVC flags) are written to the Qt/Windows docs but untested. Tell me any
  build error and I will fix it.
- **RTSP video not tested against a real camera** in this environment.
- ONVIF driver: focus, iris and one-push focus are not implemented (ISAPI has them).
- Stabilisation, heading lock and heater are vendor-specific on the CJ3000; the
  app sends nothing for them over ISAPI and shows "Failed · Not in standard ISAPI".
- Camera health beyond link/PTZ/recorder (temperature, gimbal faults) needs the
  CJ3000's own endpoints.
- MAVLink (vessel position, heading, geotags) is not integrated yet; snapshots
  say "MAVLink not linked".
- Camera passwords are stored in the per-user Qt settings store (Windows
  registry, HKCU). Move them to Windows Credential Manager before field use.
- Discovery uses multicast, which does not cross Tailscale. When you are remote,
  run discovery on the Jetson (a helper for that is not included yet) or add
  the camera by URL.

## Build on Windows with VS Code

### 1. Install once

1. **Visual Studio 2022 Build Tools** (free): workload *Desktop development with C++*.
2. **Qt 6.5 or newer** (6.8 LTS recommended) with the Qt Online Installer. Tick:
   - *MSVC 2022 64-bit*
   - *Additional Libraries → Qt Multimedia*
   - *Developer and Designer Tools → CMake* and *Ninja* (if you do not already have them)
3. **VS Code** extensions: *CMake Tools* and *C/C++* (Microsoft).
4. Optional, for recording: **ffmpeg** on PATH, e.g. `winget install Gyan.FFmpeg`.
5. Optional, for flight sticks / PTZ keyboards / generic joysticks: **SDL2**.
   Download `SDL2-devel-2.x.x-VC.zip` from the SDL GitHub releases, unzip to
   `C:\SDL2`, and set `SDL2_DIR` to `C:/SDL2/cmake` (see step 2). Without SDL2
   the app still supports Xbox-compatible gamepads through XInput.

### 2. Point the project at your Qt

Open `.vscode/settings.json` and change the Qt path to the version you
installed (and `SDL2_DIR` if you use SDL2):

```json
"CMAKE_PREFIX_PATH": "C:/Qt/6.8.3/msvc2022_64"
```

### 3. Build and run

1. *File → Open Folder* → this folder.
2. Command palette (Ctrl+Shift+P) → **CMake: Select a Kit** → *Visual Studio Build Tools 2022 Release - amd64*.
3. **CMake: Configure**, then **CMake: Build** (F7).
4. Run with **CMake: Run Without Debugging** (Shift+F5), or F5 to debug.

After each build, `windeployqt` copies the Qt DLLs and QML modules next to
`CorinthPTZ.exe`, so the exe also runs from Explorer.

Command-line alternative (from a *Developer PowerShell for VS 2022*):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build
.\build\CorinthPTZ.exe
```

## First run with the CJ3000

1. **Settings (gear) → App (local) → Cameras → CJ3000 → Edit.**
2. Enter the camera IP (Hikvision factory default is 192.168.1.64), username and password.
3. **Test connection.** "Connected · ISAPI · model" means the login and ISAPI work.
   A 401 means wrong credentials; stop before the camera locks the account.
4. **Save and display.** The live view switches to the CJ3000.
5. Video plays from `rtsp://IP:554/Streaming/Channels/101` (main) or `/102` (sub);
   channel 2 (thermal) uses `/201` and `/202`. Override the addresses in the same
   form if the CJ3000 uses different paths.

If the camera is ONVIF-only, choose **ONVIF**, enable ONVIF on the camera and
create an ONVIF user first.

## Confirming endpoints (recommended before field use)

OEM firmware can differ from standard ISAPI. For each control you care about:

1. Open the camera's own web page in Chrome, press F12 → **Network**, filter `ISAPI`.
2. Click the control (e.g. wiper, stabilisation, defog).
3. Copy the request **path** and the **XML body**.
4. Either send it to me, or add it:
   - PTZ-type actions: `src/drivers/IsapiDriver.cpp`
   - Settings fields: `src` entry in `qml/js/Schema.js`
     (`ep` = path, `tag` = XML element name, `map` for value names)

## Joystick

Settings → App → Input (or the joystick chip in the header). Pick the device
type, press buttons to see them light up, then **Assign** each action.
**Stop all** is locked so there is always a guaranteed stop. Pan/tilt and zoom
are continuous; everything else fires on press. *Take / release control* needs a
1-second hold.

## Developer modes

| Variable | Effect |
|---|---|
| `CORINTH_SELFTEST=1` | Runs 14 checks against the demo camera (presets, settings write-back, bindings, link safety, audit log) and exits with the failure count |
| `CORINTH_SHOTS=C:\path\to\folder` | Walks every screen and theme, saves PNGs, exits |

Example (PowerShell): `$env:CORINTH_SELFTEST=1; .\build\CorinthPTZ.exe`

Reference screenshots from this build are in `docs/screenshots/`.

## Where things are stored

| What | Where (Windows) |
|---|---|
| Cameras, theme, joystick bindings | Registry `HKCU\Software\Corinth\Corinth PTZ` |
| Audit log (daily CSV) | `%APPDATA%\Corinth\Corinth PTZ\logs` |
| Snapshots | `Pictures\Corinth` (change under Settings → App → Recording) |
| Recordings | `Videos\Corinth` |
| Log exports | `Documents\Corinth` |

## Project layout

```
CMakeLists.txt
src/
  main.cpp                 app entry, self-test and screenshot modes
  AppController.*          camera list, active driver, wiring
  PtzController.*          command pump, safety rules, ack, control token
  LinkMonitor.*            RTT / jitter / loss, ok / degraded / lost
  CaptureService.*         snapshots (hash + JSON), ffmpeg recording
  Discovery.*              ONVIF WS-Discovery
  JoystickManager.*        XInput / SDL2, device types, bindings
  AuditLog.*               audit model + CSV
  drivers/                 ISAPI, ONVIF, Demo, RTSP-only
qml/
  Main.qml, Theme.qml      window, design tokens
  components/              buttons, segmented control, toggles, joystick, pad…
  pages/                   LiveView, SettingsPage, CamerasPanel, InputPage, ControllerDiagram
  js/Schema.js             every Settings page and field, and its ISAPI wiring
docs/screenshots/
```
