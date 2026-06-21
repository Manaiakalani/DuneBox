# Kinect for Windows SDK 2.0 — build-time headers + import lib (vendored)

This folder contains **only** the small compile/link files needed to build the
Windows Kinect v2 backend (`ofxKinectForWindows2`):

- `inc/` — public SDK headers (`Kinect.h`, etc.)
- `Lib/x64/Kinect20.lib` — the x64 **import library** (a link-time stub; it does
  **not** contain the runtime — `Kinect20.dll` is loaded at runtime on the user's
  machine from the installed Kinect for Windows Runtime/SDK).

## Why these are vendored
The full **Kinect for Windows SDK 2.0** installer
(`KinectSDK-v2.0_1409-Setup.exe`, ~280 MB) **hangs indefinitely on headless CI
runners** because it installs the USB driver + `KinectMonitor` service, which never
completes without an interactive desktop session. The bundle also cannot be cleanly
unpacked to its MSIs by 7‑Zip (the product MSIs live in a second attached container
7‑Zip does not extract). The only files actually required to *compile and link* the
app are these headers + the 5 KB import lib, so they are checked in to make the build
fast and deterministic.

`build.yml` points `KINECTSDK20_DIR` at this folder; the addon resolves
`$(KINECTSDK20_DIR)inc` and `$(KINECTSDK20_DIR)Lib\x64\Kinect20.lib`.

## Provenance / license
These files are part of the **Microsoft Kinect for Windows SDK 2.0** (v2.0_1409) and
are © Microsoft Corporation, used under the Kinect for Windows SDK 2.0 license. They
are unmodified. End users still install the Kinect for Windows Runtime/SDK to obtain
the `Kinect20.dll` runtime + USB driver. Source:
<https://www.microsoft.com/en-us/download/details.aspx?id=44561>
