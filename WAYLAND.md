# Experimental Wayland browser docks

Status: initial implementation. Local syntax checks pass against CEF 7871,
Qt 6.4.2 and OBS 30 development headers, with and without ENABLE_WAYLAND.
Full pinned-OBS/CEF compilation passes on Ubuntu and Fedora 44. The Fedora
RPM also passes installation and executable loader checks (`--version`).
Real compositor testing is pending; this is an experimental test package.
The Fedora 44 workflow builds a separate experimental RPM under
`/opt/cherries-obs`, with a `cherries-obs-wayland` launcher and its own
configuration directory. The RPM includes x264 support and therefore uses
RPM Fusion Free for its x264 dependency. A successful package build still
needs live testing.

## Integration

Build **OBS Studio itself** with this branch as `plugins/obs-browser`,
`ENABLE_BROWSER=ON`, `ENABLE_BROWSER_PANELS=ON`, and `ENABLE_WAYLAND=ON`.
OBS's frontend compiles the inline availability check in `browser-panel.hpp`.
Replacing only `obs-browser.so` in a stock OBS installation does not remove
the old frontend's Wayland check.

The GitHub Actions workflow builds a pinned OBS source revision
`cffa83ba552f1ef6a0a05851c3aa07b3811d7e58` with this fork. It uses that OBS
revision's CEF 7871 Linux distribution and checksum, and uploads an Ubuntu
build archive only after the full build succeeds. That archive is not a
Fedora package. The separate Fedora workflow builds and installs the RPM
and runs an executable loader check. These CI checks do not exercise the
Wayland UI or browser docks.

For a local OBS source checkout with its dependencies and CEF already set up:

```sh
git -C plugins/obs-browser remote add cherries https://github.com/iBustcherries/Cherries-OBS-browser.git
git -C plugins/obs-browser fetch cherries wayland-osr-docks
git -C plugins/obs-browser checkout --detach cherries/wayland-osr-docks
cmake -S . -B build-wayland -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_BROWSER=ON -DENABLE_BROWSER_PANELS=ON -DENABLE_WAYLAND=ON \
  -DCEF_ROOT_DIR=/absolute/path/to/cef_binary_7871_linux_x86_64
cmake --build build-wayland --parallel
```

Use the build's staged/run-tree executable and CEF resources together. Do not
mix the new plugin with a different CEF runtime or overwrite the system OBS
installation for initial testing.

## Backend

- Wayland selects CEF windowless rendering and a regular Qt widget. The dock
  never obtains a native X11 handle, creates an X11 child window, sets Xdnd
  properties, or calls XConfigureWindow.
- CEF's existing Ozone platform selection remains in effect. This changes the
  panel integration; it does not itself prove that every CEF subsystem works
  with the supplied Ozone/Wayland runtime.
- CEF callbacks copy BGRA frames into shared state. A Qt timer consumes updates
  at 30 fps; no render callback dereferences a QWidget. Geometry is in device
  independent pixels and screen scale is reported to CEF.
- Dropdown popup painting, mouse, wheel, focus, common keyboard VK mapping,
  committed IME text, and common cursor shapes have initial implementations.
- X11 retains its native child-window path. The availability header checks
  for a capability export so it cannot enable Wayland with an older plugin.

## Known gaps

- `window.open` requests go to the external browser. Embedded OAuth popups,
  popup opener communication, and DevTools hosting are not implemented.
  Inspect is omitted from the Wayland context menu for this reason.
- Full IME preedit, candidate geometry, replacement ranges, custom cursors,
  accessibility, HTML drag/drop, and file dialogs still need work.
- Keyboard layouts, AltGr, clipboard, touchpad wheel scaling, high DPI monitor
  transitions, audio/video playback, and GPU behavior require live testing.
- This starts with CPU framebuffer copies and 30 fps. Large animated docks
  can use more CPU than native accelerated embedding.
- The existing panel creation/close code still shares browser ownership
  between Qt and CEF threads. Stress testing and lifecycle hardening are
  required before shipping a supported release.

## Fedora/KDE acceptance checks

1. Run on a real Plasma Wayland session with `QT_QPA_PLATFORM=wayland`.
   Confirm OBS and its CEF processes do not depend on an XWayland window.
2. Add a Custom Browser Dock for a local page and a remote chat/dashboard.
   Check text entry, Enter/Tab, copy/paste, selection, context menus, hover,
   scrolling, dropdowns, and channel/dashboard controls.
3. Resize, dock/undock, hide/show, move between monitors with different scales,
   and restore saved docks after restarting OBS.
4. Close OBS while docks are loading and while a context menu is open. Reopen
   and confirm clean shutdown, no crash, and saved cookies.
5. Repeat native browser-panel and browser-source smoke tests under X11.

Record the OBS commit, CEF version, Qt version, Fedora version, graphics
driver, and compositor with each test result. A successful CI compilation
does not replace these checks.

