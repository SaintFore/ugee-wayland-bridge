# UGEE Wayland shortcut bridge

Make the official UGEE driver's keyboard shortcuts work in native Wayland applications without modifying the vendor binaries.

Tested with **UGEE S640, driver 4.3.4, Arch Linux, niri, and Rnote**. Other tablets and compositors have not been verified. This is an unofficial companion project, not an UGEE product.

## Why it exists

The official driver sends keyboard shortcuts through X11's `XTestFakeKeyEvent`. Those events reached an XWayland test window but did not trigger actions in native Wayland Rnote. The driver's shortcut hint also stole keyboard focus, preventing rapid repeated shortcuts.

This bridge uses a process-local `LD_PRELOAD` library to replace those calls with Linux **uinput keyboard events**. It also redirects the driver's tablet XML to a private user configuration and disables the S640 shortcut hint. The original AUR driver's files stay unchanged.

## Install on Arch Linux

Install [ugee-wayland-bridge from AUR](https://aur.archlinux.org/packages/ugee-wayland-bridge):

```sh
paru -S ugee-wayland-bridge
```

The package depends on the official `ugee-tablet` AUR driver; paru resolves that dependency. Build tools from `base-devel` must be installed.

Alternatively, build from this GitHub repository:

```sh
git clone https://github.com/SaintFore/ugee-wayland-bridge.git
cd ugee-wayland-bridge
makepkg -si
```

This repository includes the `PKGBUILD` and `.SRCINFO` published on AUR. Package sources are pinned to a GitHub commit and verified with SHA-256 checksums. Updates can be installed with `paru -Syu`.

Your user must already have access to `/dev/uinput`. The current `ugee-tablet` AUR package supplies device rules; this project does not add global device permissions or require running the driver as root.

### If you use keyd

A wildcard keyd configuration can grab the bridge keyboard. Add this exclusion to the existing `[ids]` section of the relevant keyd configuration:

```ini
[ids]
-28bd:f640
# Keep your existing IDs and mappings here.
```

Then run `sudo keyd check` and `sudo keyd reload`. This package does **not** edit keyd configuration automatically.

## Use

Launch **UGEE Tablet (Wayland shortcuts)** from the application menu, or run:

```sh
ugee-tablet-wayland
```

The launcher stops existing UGEE GUI/driver processes and starts the official driver with the bridge. Use this entry instead of the original `ugeetablet` entry. Starting the original driver while the bridge is running bypasses the bridge launcher's lock. No autostart is installed.

The virtual keyboard is named `UGEE Wayland Shortcut Keyboard`, with ID `28bd:f640`. Shortcut bindings are still configured in the official panel. S640 K10 remains the vendor's show/hide-panel action.

Private configuration:

```text
${XDG_CONFIG_HOME:-~/.config}/ugee-wayland-bridge/Ugee_Tablet.xml
```

The launcher copies vendor defaults on first use and changes only S640's `BPG0611/Common/DisableInfo` to `1`. The GUI may save settings and reformat this private XML. Closing the hint prevents it from taking focus away from the drawing application. Review the private configuration after vendor driver updates; it is not automatically migrated.

Logs are replaced on each launch and stored privately at:

```text
${XDG_STATE_HOME:-~/.local/state}/ugee-wayland-bridge/driver.log
```

The current prototype logs intercepted UGEE keycodes as well as vendor startup messages. It does not monitor other applications' keyboard input.

## Scope and limitations

- This bridge changes keyboard shortcut output, not pen coordinates, pressure, mouse-button actions, or compositor tablet mapping.
- It checks common XWayland evdev mappings before using `X keycode - 8`. It rejects unsupported maps/codes rather than falling back to X11 injection.
- Repeated downs produce repeat events; redundant releases are ignored. Orderly unloading releases held keys. Abrupt termination removes the uinput device when its descriptor closes.
- Device creation failure aborts driver startup.
- The XML redirection intercepts Qt's `open64` access to paths ending in `/conf/Ugee_Tablet.xml`. A vendor update that changes configuration access or shortcut injection may require changes here.
- The launcher currently requires the S640 configuration section even though keyboard interception itself is device-independent.

## Validation

The C library builds with `-Wall -Wextra -Werror`. Integration tests exercise the actual uinput device while exclusively grabbing that test keyboard, so test shortcuts cannot affect other applications.

After installing the package, run from this directory within your graphical session:

```sh
pkexec /usr/bin/python3 "$PWD/test_bridge.py" \
  /usr/lib/ugee-wayland-bridge/libugee-wayland.so "$DISPLAY"
```

Close the running bridge first: the test discovers its virtual keyboard by name and assumes only one exists. The test connects to XWayland as the session user and uses administrator permission to read/grab the test keyboard.

Verified Ctrl+Z, Ctrl+S, Alt, repeated downs, redundant releases, invalid-code rejection, cleanup releases, and device destruction. Manual testing confirmed shortcuts and rapid repeated undo work in native Wayland Rnote after disabling the hint.

## Restore or uninstall

Close the UGEE GUI and driver, then launch the original `ugeetablet` entry to return to the unmodified vendor behavior. Remove the companion package with:

```sh
sudo pacman -R ugee-wayland-bridge
```

Remove the `-28bd:f640` exclusion from keyd if you added it, then reload keyd. User configuration and logs are retained; you may delete the two directories above if no longer needed. The official `ugee-tablet` package remains installed.

## License

MIT; see [LICENSE](LICENSE). No vendor binaries or vendor XML are distributed in this repository.
