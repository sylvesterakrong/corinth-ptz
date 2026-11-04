# Corinth PTZ

Desktop ground-station app for PTZ and gimbal cameras on the Atlantis USV.
Qt 6 + QML front end, C++ core. Runs on Windows (primary), Linux, and can be
built for Android (UniRC 7 Pro) later from the same code.

It opens with a **Demo camera** so every screen works without hardware.

## Version 0.2: what changed (from the field-test brief)

| # | Change | State |
|---|---|---|
| 1 | Night mode no longer tints the video; it restyles the interface only | Done |
| 2 | Low-latency video: in-app FFmpeg decoder (newest frame only, no buffering, auto-reconnect, RTSP or HTTP-FLV) with Settings › App › Video (latency mode, transport, preferred source) | Done; passes its stream test, not yet seen on the real camera (see "Not verified") |
| 3 | Joystick detection: SDL2 built in (no manual install), every device listed with its axes / buttons / hats, hot-plug, remembered by GUID, raw `b1..bN` + hat ids for non-gamepads, "Use detected type" | Done, tested with a virtual flight stick |
| 4 | Visible / Thermal over ONVIF: profiles grouped by video source (first = visible, second = thermal), biggest = main, next = sub; PTZ on the PTZ profile | Done, tested against a mocked two-sensor camera |
| 5 | Settings never send ISAPI to a non-ISAPI camera: one banner, fields ONVIF cannot serve say "Not supported over ONVIF" | Done |
| 6 | Settings over standard ONVIF: Imaging (brightness, contrast, saturation, sharpness, day/night, WDR, white balance, backlight compensation, exposure mode, focus mode), Media (resolution, codec, frame rate, bitrate, GOP for main / sub / third, menus limited to what the camera reports), Device (info, time mode, NTP, reboot) | Done against a fake camera; not against the real CJ3000 |
| 7 | Full-window video with floating panels; the title bar stays docked at the top like v1; in full screen only the picture remains and a ✕ drops from the top edge | Done |
| 8 | Settings content matched to the CJ3000 (resolutions, frame rates, MJPEG, third stream, BLC, focus mode, tilt range, patrol routes 1-6, presets 1-255, laser note) | Done |
| 9 | Optional credentials, HTTPS pinning, passwords in the Windows Credential Manager, "Authentication off" chip | Done; HTTPS not tested against a real TLS camera |
| 10 | Light control labelled "no illuminator fitted (per spec)", wiper "optional hardware" (CJ3000 only) | Done |
| 11 | **Zoom speed** slider (1-100, default 60, capped at 30 on a weak link) and a **Zoom level** slider that sets 1x to 40x directly | Done |

New keys: **H** hides all controls, **F11** full screen, **Esc** leaves it, **P** point mode.

## What is in this build

| Area | Status |
|---|---|
| Live view: letterboxed feed, collapsible panels, toolbar, status bar | Working |
| Day / Dark / Night (red) themes | Working |
| PTZ: pad, on-screen joystick, keyboard (WASD/arrows, Q/E, Space, 1–9, T, N, P), zoom / focus / iris hold-to-move, one-push focus, home | Working (ISAPI); focus works over ONVIF too |
| **Presets read from the camera**, go / set / add, **rename on the camera** | Working against the demo and the test camera; not yet confirmed on a real camera |
| **Click the picture to point the camera, drag a box to zoom** (key P) | ISAPI (Hikvision 3D positioning), ONVIF (RelativeMove), Demo |
| **Picture-in-picture**: visible and thermal side by side (sub stream, off when the link is weak) | Working; needs the camera to have a second channel |
| **Thermal channel**: its own stream addresses, and the Video / Image settings pages edit channel 2 | Working paths; thermal-specific settings need profile entries |
| **Aux switches with real state**: read back from the camera, flip back if refused | Working for mapped switches |
| **Camera health panel**: uptime, CPU, memory, SD card and anything else the profile lists, graded ok / warn / crit, changes logged | Working; gimbal temperature and faults need their endpoints |
| **Camera profiles**: every endpoint and XML body is data, editable without a rebuild | Working |
| **Endpoint tester**: send any request, see the raw answer, check which endpoints exist | Working |
| **Team control**: several laptops, one in control per camera, hand-over by request | Working between instances; tested with simulated laptops |
| Safety: stop on release / focus loss / link loss / control release / hand-over, 250 ms resend, speed cap 30 when degraded | Working, covered by self-test |
| Link health: RTT, jitter, loss, auto sub-stream, lost-link lock, simulate switch | Working |
| Command acknowledgement bar and audit log (on screen, daily CSV, export) | Working |
| Snapshots with SHA-256 and JSON metadata sidecar | Working |
| Recording (ffmpeg stream copy, segmented .mkv) | Working if ffmpeg is installed |
| Physical joystick: Xbox-type via XInput; flight sticks, PTZ keyboards, generic HID via SDL2 | Working; per-type bindings, deadband, expo, invert |
| Settings: 27 pages in the Hikvision layout, change tracking, Apply/Discard, re-read after write | Working; **wired fields** listed below |
| Camera discovery (ONVIF WS-Discovery) through each real LAN adapter, tells ISAPI cameras from ONVIF-only | Working on the local network segment |
| Camera drivers: ISAPI (Hikvision/OEM), ONVIF Profile S, RTSP-only, Demo | Working |

### Settings fields wired to the camera (ISAPI)

Device name, model, serial, firmware · NTP sync mode, server, interval · reboot,
restore, factory reset · DHCP, IP, mask · main and sub stream resolution, codec,
frame rate, bitrate type, max bitrate, I-frame interval · brightness, contrast,
saturation, sharpness, day/night, white balance, WDR · SD card capacity, free,
status, format · home position set/go/clear.

Everything else on the Settings pages is laid out and tracked but marked
**"Not mapped to the camera yet"**. Applying those keeps the value locally and
logs it as not sent. To wire one, add it to the camera profile (see *Camera
profiles* below): no rebuild needed.

Over ONVIF the same pages serve device info, image (brightness, contrast,
saturation, sharpness, day/night, WDR, white balance), the stream values
(read-only) and reboot. Anything else answers "Not available over ONVIF".

### Not verified (version 0.2)

Read this before the boat:

- **The low-latency player runs and passes its test, but only against streams the test serves itself.** `tests/video_test.cpp` (ctest
  `low_latency_video`) serves a live 640x360, 25 fps stream with libavformat, reads the frame number out of each delivered picture,
  and measured, on this PC over loopback: **median arrival delay 94 ms (MPEG-TS over HTTP) and 16 ms (HTTP-FLV) in low-latency mode, against
  252 ms for MPEG-TS in smooth mode**; after a 1.2 s UI stall the first picture shown was the newest, not a queued one; it reconnects by itself
  when the stream returns, and stop() returns in about 20 ms. That excludes the camera's encoder, the radio link, the screen and the
  camera's own H.264. **The "under 500 ms glass to glass" check on the real camera has not been done** (stopwatch test still needed),
  and no real RTSP stream was played: libavformat cannot serve RTSP, so the TCP / UDP choice is not covered. The test pictures are MPEG-4 and
  FLV1, not H.264 (the LGPL FFmpeg build has no H.264 encoder to make one).
- **ONVIF against the real CJ3000.** Everything ONVIF is tested against fake cameras that answer the way the standard says. Assumptions to check:
  the zoom position maps linearly from 1x (0.0) to 40x (1.0) (`kMaxZoomTimes` in `OnvifDriver.cpp`); focus speed positive means far; click to point
  needs field-of-view relative moves; the sensor order (first = visible, second = thermal); `GetVideoEncoderConfiguration` read back and written whole
  (changing the codec between H.264 and H.265 is the least certain part).
- **HTTPS**: the redirect-to-HTTPS following and the pin / ask / refuse-if-changed logic are written and the decision rule is unit-tested; a real
  TLS handshake against a camera was not run.
- **Joystick**: tested with SDL's virtual joystick (detection, counts, hot-plug, raw ids, stop on unplug, GUID memory), not with a physical device.
- **Layout** at 1920×1080 was checked for geometry only (this PC's screen is 1370×749, so it was rendered offscreen without fonts); text fit there is unchecked.
- Settings fields without a verified endpoint on this camera (one-shot focus value, 3D noise reduction, optical defog, tilt limits, patrol) stay
  unmapped until the endpoint is captured; they say "Not supported over ONVIF" on an ONVIF camera.

### Known gaps (honest list)

- **New controls are checked against fake cameras, not a real one.** The tests
  (see *Tests*) prove the app sends what the profile says and parses what a
  camera of that shape returns. Paths and bodies for presets, rename, aux
  read-back, 3D positioning and health come from the public ISAPI conventions
  and may differ on the CJ3000: use the Endpoint tester and fix the profile.
- **Preset rename** reads the preset's own XML and writes it back with a new
  name. If the CJ3000 treats a write to a preset as "store the current
  position", the rename would move it. After renaming, press Go on that preset
  once and check where it points.
- **Wiper and IR light** both use auxcontrols id 1 (as before). If the camera
  numbers them separately, change `aux.wiper.set.path` / `aux.ir.set.path`.
- **Click to point** uses the Hikvision 0–255 grid, origin top-left, start = end
  for a click. `ptz.point.scale` and `ptz.point.flipY` in the profile adjust it.
- **ONVIF**: no iris or one-push focus (ONVIF has neither as a continuous
  control); focus assumes positive speed = far; click to point needs a camera
  that supports field-of-view translation.
- Stabilisation, heading lock and heater are vendor-specific on the CJ3000: the
  switches stay greyed out ("Not mapped yet") until their endpoints are in the
  profile.
- Camera health beyond uptime / CPU / memory / SD (temperature, gimbal faults)
  needs the CJ3000's own endpoints in the profile.
- **Team control** is a shared-key protocol over UDP, not an access control
  system: the camera itself does not enforce who is in control, and any laptop
  with the key can take over. Windows may ask to allow UDP port 47620 through
  the firewall the first time.
- MAVLink (vessel position, heading, geotags) is not integrated yet; snapshots
  say "MAVLink not linked".
- Camera passwords and the team key are stored in the per-user Qt settings
  store (Windows registry, HKCU). Move them to Windows Credential Manager before
  field use.
- Discovery uses multicast, which does not cross Tailscale. When you are remote,
  run discovery on the Jetson (a helper for that is not included yet) or add
  the camera by URL.
- RTSP video not tested against a real camera in this environment.
- Built and tested on Windows 11 with Qt 6.10.2 (MinGW 13.1). The MSVC build
  paths are written to the Qt/Windows docs but untested.

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

### SDL2 and FFmpeg (downloaded for you)

- **SDL2** (joysticks) is downloaded at configure time (release 2.30.9, SHA-256 checked) and built statically, so nothing needs
  installing. To use an installed one instead, set `SDL2_DIR`. `-DCORINTH_WITH_SDL2=OFF` turns joystick support to XInput only.
- **FFmpeg** (the low-latency player) needs its development files. Either point `-DFFMPEG_DIR=<folder with include/ lib/ bin/>` at a
  *shared* FFmpeg 6 to 8 build, or leave it empty on Windows and a pinned LGPL shared build (FFmpeg 7.1.5, SHA-256 checked) is
  downloaded and its DLLs copied next to the exe. `-DCORINTH_FETCH_FFMPEG=OFF` skips the download; without FFmpeg the app still builds and
  plays video through Qt's MediaPlayer (more delay), and the status says so. FFmpeg is LGPL: keep it as DLLs, do not link it statically.
- Tested here with Qt 6.10.2 MinGW 13.1 (Ninja). The brief names MSVC 2022 and Qt 6.8; the code uses nothing MinGW-specific, but that build was not run.
- For the least video delay set the camera's I-frame interval equal to its frame rate (25), and use the sub stream over a radio link.

### Building with the MinGW kit (Qt 6.10.2, as installed on the developer PC)

From PowerShell in this folder (the first line only lasts for that window):

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\6.10.2\mingw_64\bin;" + $env:PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.2/mingw_64
cmake --build build
.\build\CorinthPTZ.exe
```

After the first time, only the PATH line and `cmake --build build` are needed. The build copies the
Qt DLLs and the three MinGW runtime DLLs (`libgcc_s_seh-1`, `libstdc++-6`, `libwinpthread-1`) next to
the exe, so double-clicking `build\CorinthPTZ.exe` works; move the whole `build` folder to run it elsewhere.

## First run with the CJ3000

The CJ3000 (SOWZE SW-PTW2040HC) is controlled over **ONVIF**, not Hikvision ISAPI.

1. **Settings (gear) → App (local) → Cameras → CJ3000 → Edit.** Protocol **ONVIF**.
2. Enter the camera IP. With the camera's ONVIF authentication switched off, leave **username and password blank**: nothing is sent,
   and the camera is listed with an "Authentication off" chip as a reminder to keep it on an isolated network. If authentication is
   on, fill them in (stored in the Windows Credential Manager, not the registry).
3. **Test connection.** "Connected · no authentication · ONVIF profiles and stream address read" (or "Connected · ONVIF …" with a login).
4. **Save and display.** The live view switches to the CJ3000. The stream addresses are read from the camera (`GetStreamUri`); the
   first video source is the visible camera, the second the thermal one. Override the addresses in the same form if needed, and the
   camera's HTTP-FLV address (for example `http://IP/101.flv`) if you want to try it under Settings › App › Video.
5. If the camera redirects to HTTPS you are shown its certificate fingerprint once; trusting it pins that exact certificate.

For a Hikvision-style camera choose **ISAPI** instead (video on `rtsp://IP:554/Streaming/Channels/101` and `/102`, thermal `/201` and `/202`).

## Camera profiles: wiring endpoints without a rebuild

Every request the app sends to an ISAPI camera (move, presets, focus, wiper, status,
health, 3D positioning, image paths, stream addresses) is described in a **camera
profile**. The built-in one is `profiles/hikvision-isapi.json`. Your changes go in
files that override it key by key:

| File | Applies to |
|---|---|
| `%APPDATA%\Corinth\Corinth PTZ\profiles\default.json` | every camera on this laptop |
| `%APPDATA%\Corinth\Corinth PTZ\profiles\<cameraId>.json` | one camera (wins over default) |

**Settings › App › Endpoint tester** shows both paths, creates starter files, opens
the folder, and has a **Reload profile** button: edit, save, reload, no restart.
A key set to `null` removes a built-in entry. A broken file is reported and ignored.

### Confirming endpoints

1. Open the camera's own web page in Chrome, press F12 → **Network**, filter `ISAPI`.
2. Click the control (wiper, stabilisation, defog …). Copy the request **method**,
   **path** and **XML body**; for a switch, also the GET that reads its state.
3. In the Endpoint tester, paste the path (and body), press **Send**, and check the
   answer. **Check known endpoints** asks the camera about ~25 standard paths and every
   path in the profile (200 exists, 401 wrong login, 404 not on this firmware).
4. **Copy request as profile entry**, paste it into your profile file where it belongs
   (below), save, **Reload profile**.

Writes (PUT, POST, DELETE) from the tester need this laptop to be in control with the
link up, ask twice for paths that can reboot or erase the camera, and are logged
(method, path and result; never the body).

### Profile reference

Placeholders in `path` and `body`: `{ch}` PTZ channel, `{vch}` the channel being
viewed (1 visible, 2 thermal), `{id}` preset number, `{name}`, `{pan}` `{tilt}` `{zoom}`,
`{speed}`, `{state}`, `{x1}` `{y1}` `{x2}` `{y2}`. Values placed in a body are XML-escaped.

```jsonc
{
  "channel": 1, "channels": 2,                 // PTZ channel; 2 = camera has a thermal channel
  "ptz": {
    "continuous": { "method": "PUT", "path": "...", "body": "<...>" },
    "stop":       { ... },                      // optional separate stop endpoint
    "gotoPreset", "setPreset", "listPresets", "getPreset", "renamePreset",
    "gotoHome", "setHome", "focus", "iris", "onePushFocus",
    "point":  { ..., "scale": 255, "flipY": false },   // click to point / box zoom
    "status": { "path": "...", "azimuth": "azimuth", "elevation": "elevation",
                "zoom": "absoluteZoom", "divisor": 10 }
  },
  "aux": {                                      // the switches on the Aux tab
    "stab": {
      "order": 1, "label": "Stabilisation", "on": "enable", "off": "disable",
      "set": { "method": "PUT", "path": "...", "body": "<S>{state}</S>" },
      "get": { "method": "GET", "path": "...", "tag": "status",
               "matchTag": "type", "matchValue": "WIPER" }   // matchTag/Value: pick one block out of a list
    }
  },
  "health": {                                   // the Health tab; every item reads one XML tag
    "gimbalTemp": { "order": 6, "label": "Gimbal temperature", "ep": "/ISAPI/...", "tag": "temperature",
                    "unit": " °C", "warnAbove": 60, "critAbove": 75 }
    // also: warnBelow, critBelow, okValues / warnValues / critValues (lists), badLevel, format: "duration"
  },
  "settings": { "defog": { "ep": "/ISAPI/...", "tag": "DehazeMode", "on": "open", "off": "close" } },
  "actions":  { "reboot": { "method": "PUT", "path": "/ISAPI/System/reboot" } },
  "stream":   { "url": "rtsp://{ip}:{rtspPort}/Streaming/Channels/{channel}0{n}" },
  "image":    { "color": "/ISAPI/Image/channels/{vch}/color", "ircut": "..." }
}
```

`settings` adds or replaces the camera wiring (`ep`, `tag`, `map`, `scale`, `on`, `off`) of a
field on the Settings pages, by the field's id from `qml/js/Schema.js`. `actions` does the same
for the buttons. Camera stream addresses (including thermal) are set per camera under
**Settings › App › Cameras › Edit**.

## Team control (several laptops)

**Settings › App › Team control**: enter the same **team key** on every laptop. Laptops on
the same network find each other (UDP multicast, port 47620). Over Tailscale, list the other
laptops' addresses under *Other laptops* (multicast does not cross it). With no key the app
behaves as before: control stays on this laptop.

One laptop at a time is **in control of each camera**; the others are view-only.

- Starting up, a laptop listens for about 3 s before taking control of a camera nobody holds.
- **Request control** asks the holder, who sees *Hand over* / *Keep control*. Handing over
  stops the camera first, and the new holder sends a stop when it takes over.
- **Take over…** is the override for a holder that cannot answer (always works, asks
  you to confirm, locks them at once, logged as a warning).
- A holder that stops announcing for 5 s is treated as gone.
- Messages are signed with the key (HMAC-SHA256); unsigned, wrongly signed and replayed
  messages are ignored. The key is not encrypted in the registry.

## Health, presets, aux, thermal

- **Health tab / rail**: readings every 15 s while the link is good (paused and shown
  stale when it is degraded or lost). A reading turning warn or crit, and recovering,
  is written to the audit log.
- **Presets** are read from the camera on connect and **Refresh**. If the list cannot
  be read, the names stored on this laptop are used and the tab says so.
- **Aux** switches show their real state, dimmed when unknown; a refused command flips
  the switch back and the last-command bar says why.
- **Thermal**: set the thermal stream addresses under Cameras › Edit (or rely on
  `Streaming/Channels/201` and `202`). The Visible/Thermal switch, PiP button and the
  Visible/Thermal switch on the Video and Image settings pages appear when the camera
  has a second channel (`"channels": 2` in the profile, or a thermal URL set).

## Joystick

Settings → App → Input (or the joystick chip in the header). Pick the device
type, press buttons to see them light up, then **Assign** each action.
**Stop all** is locked so there is always a guaranteed stop. Pan/tilt and zoom
are continuous; everything else fires on press. *Take / release control* needs a
1-second hold.

## Developer modes

| Variable | Effect |
|---|---|
| `CORINTH_SELFTEST=1` | Runs 26 checks against the demo camera (presets, rename, aux, click to point, health, profile, settings write-back, bindings, link safety, audit log) and exits with the failure count |
| `CORINTH_SELFTEST=2` | Team-control hand-over through the real controller, against a simulated second laptop (needs the variables below) |
| `CORINTH_SHOTS=C:\path\to\folder` | Walks every screen and theme, saves PNGs, exits |
| `CORINTH_WINDOW=1920x1080` | Opens the window at that size (it cannot exceed the screen; combine with `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software` for a size the screen cannot show, but that renders without fonts) |
| `CORINTH_INSTANCE=name` | Separate settings, camera list and logs for this run, so a second copy on one PC never touches the real ones. The self-test and screenshot modes use their own store (`test`) automatically, so they never change your cameras or joystick bindings |
| `CORINTH_CONTROL_KEY`, `CORINTH_CONTROL_PORT`, `CORINTH_CONTROL_PEERS` | Override the team-control settings (testing) |

Example (PowerShell): `$env:CORINTH_SELFTEST=1; .\build\CorinthPTZ.exe`.
When output is piped, set `QT_ASSUME_STDERR_HAS_CONSOLE=1` or Qt on Windows will not print it.

To try team control on one PC, start two copies with different `CORINTH_INSTANCE`,
`CORINTH_CONTROL_KEY` (same), `CORINTH_CONTROL_PORT` (different) and
`CORINTH_CONTROL_PEERS=127.0.0.1:<the other port>`.

Reference screenshots from this build are in `docs/screenshots/`.

## Tests

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\6.10.2\mingw_64\bin;" + $env:PATH
cmake --build build
ctest --test-dir build --output-on-failure
```

| Test | What it checks |
|---|---|
| `camera_drivers` | ISAPI and ONVIF drivers against an in-process fake camera: what is sent (paths, bodies), what is parsed back, profile override files |
| `control_coordinator` | Hand-over, request / grant / deny, override, silent holder, forged and replayed messages, one token per camera |
| `joystick_input` | SDL joystick detection with a virtual flight stick: listing and counts, hot-plug, raw button / hat ids under any diagram, bound actions, stop on unplug, remembered by GUID |
| `credential_store` | Camera passwords in the Windows Credential Manager: write, read, replace, remove, non-ASCII |
| `low_latency_video` | (only when FFmpeg is configured) The FFmpeg player against a live MPEG-TS and FLV stream: delivered frames, arrival delay, newest-frame-wins after a stall, reconnect, stop |
| `camera_discovery` | Discovery through the real LAN adapter against fake devices (ISAPI vs ONVIF-only detection); skips if there is no adapter |

These prove the code against the protocol as written, not against a real camera.

## Where things are stored

| What | Where (Windows) |
|---|---|
| Cameras, theme, joystick bindings, team key, tester history | Registry `HKCU\Software\Corinth\Corinth PTZ` |
| Camera profile overrides | `%APPDATA%\Corinth\Corinth PTZ\profiles` |
| Audit log (daily CSV) | `%APPDATA%\Corinth\Corinth PTZ\logs` |
| Snapshots | `Pictures\Corinth` (change under Settings → App → Recording) |
| Recordings | `Videos\Corinth` |
| Log exports | `Documents\Corinth` |

**Opening them:** Settings › App › Recording has *Open the snapshots folder*, *Open the recordings folder* and *Open the audit log folder*;
the snapshot card in the live view has *Open folder*; the Log tab opens the export and log folders. Each snapshot is a PNG plus a `.json`
sidecar with its UTC time, camera, position, channel, operator and SHA-256. A folder set under Recording replaces the default.
Recording runs `ffmpeg.exe`, which the build copies next to the app (from the FFmpeg package); without it, or on PATH, the Record button
reports "ffmpeg not found" and nothing is saved.

## Project layout

```
CMakeLists.txt
profiles/
  hikvision-isapi.json     built-in camera profile (endpoints and bodies as data)
src/
  main.cpp                 app entry, self-test and screenshot modes
  AppController.*          camera list, active driver, presets, profile helpers, wiring
  PtzController.*          command pump, safety rules, ack, aux state, control token
  ControlCoordinator.*     team control: shared token between laptops over UDP
  HealthMonitor.*          polls and grades the profile's health items
  LinkMonitor.*            RTT / jitter / loss, ok / degraded / lost
  CaptureService.*         snapshots (hash + JSON), ffmpeg recording
  Discovery.*              ONVIF WS-Discovery per LAN adapter, ISAPI detection
  NetUtil.*                which network adapters to use (skips VPN / virtual)
  JoystickManager.*        XInput / SDL2, device types, bindings
  AuditLog.*               audit model + CSV
  drivers/                 CameraProfile, ISAPI, ONVIF, Demo, RTSP-only
qml/
  Main.qml, Theme.qml      window, design tokens
  components/              buttons, segmented control, toggles, joystick, pad…
  pages/                   LiveView, SettingsPage, CamerasPanel, EndpointTester, InputPage, ControllerDiagram
  js/Schema.js             every Settings page and field, and its ISAPI wiring
tests/                     camera_drivers, control_coordinator, camera_discovery
docs/screenshots/
```
