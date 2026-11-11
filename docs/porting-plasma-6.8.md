# Porting SevenPlasma to Plasma 6.8 (Wayland)

Survey made on 2026-11-11. SevenPlasma 0.1 targets Kubuntu 26.04 (Plasma 6.6 LTS, KDE Frameworks 6.24,
Qt 6.10) in an **X11** session. Every other KDE distribution has moved on: Arch, Fedora 44/45 and openSUSE
Tumbleweed ship Plasma 6.8 (Fedora 43: 6.7), and Plasma 6.8 has **no X11 session** any more.
AeroThemePlasma's current branch is `Plasma/6.8` ("Wayland, X11 (before Plasma 6.8)"). Supporting other
distributions therefore means supporting Plasma 6.8 on Wayland. Kubuntu 26.04 stays supported (Plasma 6.6 gets
fixes until 2029), so the X11 code paths stay too, next to the new ones.

Versions on Arch at the time: plasma-workspace / kwin 6.8.2, KIO 6.31.0, Dolphin 26.08.3, Kvantum 1.1.9,
Qt 6.12.0.

## 1. Our patches against the new upstream versions

Applied with `git apply --reject` to the current upstream trees:

| Patch | Hunks | Rejected | Notes |
|---|---|---|---|
| aerothemeplasma (`Plasma/6.8`) | 112 | 1 + 1 | `install.sh` (we do not use it), lock screen `AuthUI.qml`; the desktop containment sources moved (`plasma/plasmoids/src/desktopcontainment` -> `io.gitgud.wackyideas.desktopcontainment`, 4 files incl. `foldermodel.cpp`) |
| atp-local-overrides | 177 | 8 | Start menu (2 files), desktop `FolderItemDelegate.qml` (4), `FolderViewLayer.qml` |
| atp-sddm | 19 | 0 | applies cleanly, but see 3.1 (greeter on Wayland) |
| kio (6.24 -> 6.31) | 83 | 3 | small |
| kvantum (1.1.5 -> 1.1.9) | 20 | 0 | |
| dolphin-aero (on Dolphin 26.08) | 130 | 24 + 2 missing files | dolphin-aero itself (atmk) exists only for Dolphin 25.12: the Explorer fork has to be moved to 26.08 first |
| libplasma, smod, kwin components, workspace, uac, libaero-qt | 36 | not tested | small patches; follow the `Plasma/6.8` branches of the AeroShell repositories |

Applying cleanly does not mean working: the QML uses Plasma APIs that may have changed between 6.6 and 6.8.
That can only be seen on a running 6.8 system.

## 2. X11-only code in our programs

| Area | Where | What it does on X11 | On Wayland | Size |
|---|---|---|---|---|
| Shutdown screen | `data/lib/seven-shutdown.py` | override-redirect window raised every 10 ms, `XQueryTree` to wait for other windows | layer-shell overlay window (LayerShellQt, layer "overlay"); the window list is not needed | M |
| Logon / Welcome | `apps/seven-common/boot` (`seven-sddm-xsetup`, SDDM `-background none`, KWin `loginaero` effect) | the X root keeps the "Welcome" frame until the session splash | SDDM Wayland greeter (or Plasma Login Manager on Fedora); the seamless hand-over needs another way | M |
| Snipping Tool | `apps/seven-snip` | window list (`_NET_CLIENT_LIST_STACKING`), frame extents, startup notification, skip-taskbar, transient, screen grab with `QScreen::grabWindow` | screenshots through KWin `org.kde.KWin.ScreenShot2` (desktop file must ask for it), window picking through KWin; a fullscreen layer-shell surface for the whitened screen | L |
| Task Manager, Applications tab | `apps/seven-taskmgr` | `KX11Extras` stacking order, icons, minimize, activate, `NETRootInfo` close | `libtaskmanager` (plasma-workspace) works on both | M |
| Gadgets | `apps/seven-gadgets` | keep-below, skip taskbar, all desktops (`KX11Extras`) | layer-shell "bottom" layer or a KWin rule | S |
| On-Screen Keyboard | `win7access.h` | keys sent with XTest, layout labels from Xlib/XKB | KWin fake-input / virtual-keyboard protocol, or Plasma 6.6+'s own keyboard | L |
| Mouse buttons, pointer | `win7mouse.h` | `xinput set-prop` | KWin `org.kde.KWin.InputDevice` D-Bus (kcminputrc keeps working) | S |
| Display, ClearType, devices | `win7display.h`, `win7cttune.h`, `win7devices.h` | `xrandr` (modes, primary, EDID, the 1366 scaling trick) | libkscreen / `kscreen-doctor` | M |
| Screen saver | `win7screensaver.h` | bypass-WM fullscreen windows, `grabWindow` | layer-shell fullscreen, screenshot through KWin | M |
| Visual effects | `win7visualfx.h` | suspends compositing at login | Wayland is always composited: the option goes | S |
| Window class | `seven-control/main.cpp` | `WM_CLASS` through xcb (KWin rules, smodrc exceptions) | app id = desktop file name (`setDesktopFileName`); smodrc exceptions by app id | S |
| NVIDIA / AMD / Intel panels | `apps/seven-nvidia`, `seven-ccc`, `seven-igfx` | RandR CTM, gamma ramps through XRandR, `xrandr` | no client API for gamma on Wayland: colour settings through KWin (ICC profiles, brightness) or left out | L |
| "Always on top" | WMP, Task Manager, CCC | `Qt::WindowStaysOnTopHint` | ignored on Wayland: KWin script / rule through D-Bus | S |
| Window placement | 169 `move()` / `setGeometry()` calls, popups placed at screen coordinates (flyouts, drag ghosts, test windows) | free placement | clients cannot place toplevel windows: popups need a parent (xdg-popup), the rest is placed by KWin; to be checked one by one | M |
| Keyboard layouts at logon | `seven-keyboard-sync` | writes `/etc/default/keyboard` and `00-keyboard.conf` | same files on Ubuntu; on Arch / Fedora through `localectl` (their localed may write X11 settings) | S |

S = hours, M = a day or two, L = several days.

## 3. Other work

1. **Installer per distribution.** Package lists for pacman (Arch) and dnf (Fedora), library paths
   (`/usr/lib` instead of `/usr/lib/x86_64-linux-gnu`), the initramfs tool for the boot screen (mkinitcpio,
   dracut), the version checks. `uninstall.sh` finds package owners with dpkg: `pacman -Qo` / `rpm -qf`
   instead.
2. **Session.** No `kwin_x11`, no X11 session file: the AeroThemePlasma Wayland session; `seven-firstrun`
   already recognises the AeroThemePlasma shell by `PLASMA_DEFAULT_SHELL`.
3. **Build.** Qt 6.12 and Frameworks 6.31: deprecations and API changes in our programs (not tried yet).
4. **Test machines.** An Arch VM (and Fedora later) next to the Kubuntu one, with snapshots like
   `~/VMs/sevenplasma-test`.

## 4. Suggested order

1. Arch VM with Plasma 6.8 and upstream AeroThemePlasma (`Plasma/6.8`), as its install guide says.
2. Port our AeroThemePlasma patches (10 rejected hunks, the moved desktop containment) and see the shell,
   logon, lock screen and UAC run.
3. Build our programs there; fix what does not compile.
4. Wayland replacements by importance: window class / app ids, Task Manager, gadgets, shutdown screen,
   always-on-top, window placement; then the Snipping Tool, display pages, on-screen keyboard, screen saver.
5. Dolphin 26.08: port dolphin-aero and our patch (or wait for dolphin-aero upstream).
6. `install.sh` / `uninstall.sh` for Arch, the same VM test as on Kubuntu (install, first login,
   uninstall, compare).
7. Fedora after Arch.
