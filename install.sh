#!/usr/bin/env bash
# SevenPlasma - the Windows 7 experience for KDE Plasma 6 (X11), based on AeroThemePlasma.
#
#   ./install.sh [--iso Windows7.iso] [--no-games] [--no-forks] [--yes]
#
# --iso FILE   your own Windows 7 image: icons, sounds, fonts, wallpapers, the boot animation, the games
#              and the pictures of the programs are taken from it on your machine (nothing of Microsoft's
#              is part of SevenPlasma). Without it the theme still installs, with free replacements.
# --no-games   do not install the Windows 7 games (they need Wine)
# --no-forks   do not build the patched Dolphin / KIO / Kvantum (Explorer look, Windows 7 file dialogs)
#
# Tested on Kubuntu 26.04 (Plasma 6.6, KDE Frameworks 6.24, Dolphin 25.12, Kvantum 1.1.5), X11 session.
set -Eeuo pipefail

REPO=$(cd "$(dirname "$0")" && pwd)
# messages in Ukrainian when the system language is Ukrainian, in English otherwise
case "${LANGUAGE:-${LC_ALL:-${LANG:-}}}" in uk*) UK=1 ;; *) UK=0 ;; esac
L() { if [ "$UK" = 1 ]; then printf '%s' "$1"; else printf '%s' "$2"; fi; }
WORK=${XDG_CACHE_HOME:-$HOME/.cache}/sevenplasma-build
LOG=$WORK/install.log
ISO=""
GAMES=1
FORKS=1
YES=0
JOBS=$(nproc)

while [ $# -gt 0 ]; do
    case "$1" in
        --iso) ISO=$(readlink -f "$2"); shift 2 ;;
        --no-games) GAMES=0; shift ;;
        --no-forks) FORKS=0; shift ;;
        --yes|-y) YES=1; shift ;;
        -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "$(L "Невідомий параметр: $1 (див. ./install.sh --help)" "Unknown option: $1 (see ./install.sh --help)")"; exit 1 ;;
    esac
done

mkdir -p "$WORK"
: > "$LOG"
STEP=0
TOTAL=8
step() { STEP=$((STEP + 1)); printf '\n[%d/%d] %s\n' "$STEP" "$TOTAL" "$1"; echo "=== $1" >> "$LOG"; }
run() { "$@" >> "$LOG" 2>&1; }
note() { printf '      %s\n' "$*"; }
warn() { printf '      ! %s\n' "$*"; }
die() { printf "\n$(L 'Помилка' 'Error'): %s\n$(L 'Подробиці' 'Details'): %s\n" "$*" "$LOG"; exit 1; }
trap 'die "$(L "команда завершилася невдало (рядок $LINENO)" "a command failed (line $LINENO)")"' ERR

# ------------------------------------------------------------------ checks
[ "$(id -u)" -ne 0 ] || die "$(L "запускайте від звичайного користувача, не від root (пароль спитають, коли треба)" "run as your normal user, not as root (the password is asked when needed)")"
command -v apt-get >/dev/null || die "$(L "потрібен дистрибутив на основі Ubuntu / Debian (Kubuntu)" "an Ubuntu / Debian based distribution is needed (Kubuntu)")"
command -v plasmashell >/dev/null || die "$(L "KDE Plasma не встановлено" "KDE Plasma is not installed")"
PLASMA=$(dpkg-query -W -f='${Version}' plasma-workspace 2>/dev/null | sed 's/^[0-9]*://; s/-.*//')
case "$PLASMA" in 6.6.*) ;; *) die "$(L "потрібна Plasma 6.6 (знайдено: ${PLASMA:-?})" "Plasma 6.6 is needed (found: ${PLASMA:-?})")" ;; esac
[ -z "$ISO" ] || [ -f "$ISO" ] || die "$(L "файл образу не знайдено: $ISO" "image file not found: $ISO")"
[ "${XDG_SESSION_TYPE:-x11}" = x11 ] || warn "$(L "зараз сесія $XDG_SESSION_TYPE; SevenPlasma розрахована на X11 (її можна вибрати на екрані входу)" "this is a $XDG_SESSION_TYPE session; SevenPlasma is made for X11 (choose it on the logon screen)")"

echo "$(L "SevenPlasma: вигляд Windows 7 для KDE Plasma" "SevenPlasma: the Windows 7 experience for KDE Plasma")"
echo "$(L "Журнал встановлення: $LOG" "Install log: $LOG")"
[ -n "$ISO" ] && echo "$(L "Образ Windows 7: $ISO" "Windows 7 image: $ISO")" || echo "$(L "Без образу Windows 7: будуть вільні замінники (іконки й звуки ATP, шрифт Selawik), без ігор." "No Windows 7 image: free replacements will be used (ATP icons and sounds, the Selawik font), no games.")"
if [ $YES -eq 0 ]; then
    read -r -p "$(L "Продовжити? [Т/н] " "Continue? [Y/n] ")" a
    case "$a" in ""|[TtYyТт]*) ;; *) exit 0 ;; esac
fi
sudo -n true 2>/dev/null || sudo -v || die "$(L "потрібні права адміністратора" "administrator rights are needed")"
# keep sudo alive while building
( while true; do sudo -n true; sleep 50; kill -0 "$$" 2>/dev/null || exit; done ) 2>/dev/null &

# pinned upstream versions the patches are made for: name url commit
pin() { awk -v n="$1" '$1 == n {print $2, $3}' "$REPO/patches/BASES.txt"; }
fetch() {   # fetch NAME DIR -> checkout of the pinned commit with our patch applied
    local name=$1 dir=$2 url commit
    read -r url commit < <(pin "$name")
    if [ ! -d "$dir/.git" ]; then run git clone --filter=blob:none "$url" "$dir"; fi
    run git -C "$dir" fetch --quiet origin
    run git -C "$dir" checkout --quiet --force "$commit"
    run git -C "$dir" clean -fdq -e build -e 'build*'
    if [ -s "$REPO/patches/$name.patch" ]; then run git -C "$dir" apply --whitespace=nowarn "$REPO/patches/$name.patch"; fi
}
cmake_install() {   # cmake_install SRC BUILD [cmake args...]   -> builds and installs to /usr with sudo
    local src=$1 build=$2; shift 2
    run cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_CXX_FLAGS="-I$WORK/src" "$@"
    mkdir -p "$build/bin/x11"   # the KWin effects link to bin/x11/NAME.so, but nothing creates that folder
    run cmake --build "$build" -j"$JOBS"
    run sudo cmake --install "$build"
}

# ------------------------------------------------------------------ 1. packages
step "$(L "Встановлення пакетів" "Installing packages")"
PKGS=(git gettext cmake ninja-build build-essential extra-cmake-modules curl unzip 7zip python3-pil python3-pyqt6
      qt6-base-dev qt6-svg-dev qt6-multimedia-dev qt6-tools-dev qt6-5compat-dev qt6-declarative-dev
      libqt6virtualkeyboard6 libqt6multimedia6 libqt6core5compat6
      libkf6baloo-dev libkf6config-dev libkf6filemetadata-dev libkf6idletime-dev libkf6kio-dev libkf6service-dev
      libkf6solid-dev libkf6statusnotifieritem-dev libkf6windowsystem-dev libkf6auth-dev libkf6iconthemes-dev
      libpackagekitqt6-dev libfontconfig-dev libfreetype-dev
      libplasma5support6 libkdecorations3-dev libkf6colorscheme-dev libkf6i18n-dev libkf6kcmutils-dev libkirigami-dev
      libkf6notifications-dev libkf6svg-dev libkf6crash-dev libkf6globalaccel-dev libplasma-dev libplasmaactivities-dev
      libxcb-composite0-dev libxcb-randr0-dev libxcb-shm0-dev libxcb-damage0-dev libepoxy-dev kwin-dev kwin-x11 kwin-x11-dev
      plasma-wayland-protocols libkf6qqc2desktopstyle-dev libplasma5support-dev libkf6newstuff-dev
      libkf6notifyconfig-dev libkf6attica-dev libkf6runner-dev libkf6dbusaddons-dev libkf6sonnet-dev libkf6xmlgui-dev
      libkf6coreaddons-dev libkf6widgetsaddons-dev libkf6guiaddons-dev qml-module-org-kde-qqc2desktopstyle
      libplasmaactivitiesstats-dev plasma-workspace-dev qml6-module-org-kde-kirigamiaddons-sounds
      libpolkit-qt6-1-dev libkf6parts-dev libkf6style-dev libksysguard-dev libbaloowidgets-dev libkf6textwidgets-dev
      libkf6userfeedback-dev libkf6archive-dev libkf6bookmarks-dev libkf6codecs-dev libkf6completion-dev libkf6doctools-dev
      libkf6itemviews-dev libkf6jobwidgets-dev libkf6configwidgets-dev libkf6itemmodels-dev libkf6package-dev qt6-wayland-dev
      libdrm-dev qt6-wayland-private-dev wayland-protocols libwayland-dev libxslt1-dev libxml2-dev
      qt6-style-kvantum plymouth plymouth-themes ufw libxtst6 x11-xserver-utils)
[ $GAMES -eq 1 ] && [ -n "$ISO" ] && PKGS+=(wine)
run sudo apt-get -o DPkg::Lock::Timeout=900 update
run sudo apt-get -o DPkg::Lock::Timeout=900 install -y --no-install-recommends "${PKGS[@]}"
note "$(L "готово" "done")"

# ------------------------------------------------------------------ 2. AeroThemePlasma
step "$(L "AeroThemePlasma (оболонка, рамки вікон, вхід, UAC) з нашими змінами" "AeroThemePlasma (shell, window frames, logon, UAC) with our changes")"
mkdir -p "$WORK/atp/repos" "$WORK/src"
ln -sfn "$REPO/apps/seven-common" "$WORK/src/seven-common"
for r in libplasma uac-polkit-agent smod aeroshell-workspace aeroshell-kwin-components aeroshell-sddm-kcm; do
    note "$r"
    fetch "atp-$r" "$WORK/atp/repos/$r"
done
# the icon and sound packs are AeroThemePlasma's own (Windows resources): installed from its repositories
for r in aerothemeplasma-icons aerothemeplasma-sounds; do
    [ -d "$WORK/atp/repos/$r/.git" ] || run git clone --depth 1 "https://gitgud.io/aeroshell/atp/$r" "$WORK/atp/repos/$r"
done
fetch aerothemeplasma "$WORK/atp/main"
run git -C "$WORK/atp/main" apply --whitespace=nowarn "$REPO/patches/atp-local-overrides.patch"
run git -C "$WORK/atp/main" apply --whitespace=nowarn "$REPO/patches/atp-sddm.patch"
R=$WORK/atp/repos
cmake_install "$R/libplasma" "$R/libplasma/build"
cmake_install "$R/uac-polkit-agent" "$R/uac-polkit-agent/build" -DCMAKE_INSTALL_LIBEXECDIR=lib
cmake_install "$R/smod" "$R/smod/build"
cmake_install "$R/smod" "$R/smod/build-x11" -DBUILD_DECORATION=OFF -DBUILD_EFFECTX11=ON
cmake_install "$R/aeroshell-workspace" "$R/aeroshell-workspace/build"
run sudo update-mime-database /usr/local/share/mime || true
cmake_install "$R/aeroshell-kwin-components" "$R/aeroshell-kwin-components/build" -DKWIN_BUILD_WAYLAND=ON
cmake_install "$R/aeroshell-kwin-components" "$R/aeroshell-kwin-components/build_x11" -DKWIN_BUILD_WAYLAND=OFF -DKWIN_INSTALL_MISC=OFF
cmake_install "$R/aeroshell-sddm-kcm" "$R/aeroshell-sddm-kcm/build"
cmake_install "$R/aerothemeplasma-icons" "$R/aerothemeplasma-icons/build"
cmake_install "$R/aerothemeplasma-sounds" "$R/aerothemeplasma-sounds/build"
cmake_install "$WORK/atp/main" "$WORK/atp/main/build" -DCMAKE_INSTALL_LIBEXECDIR=lib
cmake_install "$WORK/atp/main" "$WORK/atp/main/build_x11" -DCMAKE_INSTALL_LIBEXECDIR=lib -DINSTALL_X11_COMPONENTS=ON
# the small user tile of an account without a password (lock screen) uses the logon screen's frames
SH=/usr/share/plasma/shells/io.gitgud.wackyideas.desktop/contents/images/pfp
A=/usr/share/sddm/themes/sddm-theme-mod/Assets
run sudo install -m 644 "$A/12222.png" "$SH/small.png"
run sudo install -m 644 "$A/12218.png" "$SH/small-hover.png"
run sudo install -m 644 "$A/12219.png" "$SH/small-focus.png"
run sudo install -m 644 "$A/12220.png" "$SH/small-focus-hover.png"
note "$(L "готово" "done")"

# ------------------------------------------------------------------ 3. Explorer (Dolphin / KIO / Kvantum forks)
step "$(L "Провідник, діалоги файлів і стиль вікон (Dolphin, KIO, Kvantum)" "Explorer, file dialogs and widget style (Dolphin, KIO, Kvantum)")"
v() { dpkg-query -W -f='${Version}' "$1" 2>/dev/null | sed 's/^[0-9]*://; s/-.*//'; }
fetch libaero-qt "$WORK/src/libaero-qt"
cmake_install "$WORK/src/libaero-qt" "$WORK/src/libaero-qt/build"
if [ $FORKS -eq 1 ]; then
    if [ "$(v libkf6kio-dev)" = 6.24.0 ]; then
        fetch kio "$WORK/src/kio"
        cmake_install "$WORK/src/kio" "$WORK/src/kio/build" -DBUILD_TESTING=OFF
        run msgfmt -o "$WORK/kio6-win7.mo" "$WORK/src/kio/po-win7/uk.po" && run sudo install -m 644 "$WORK/kio6-win7.mo" /usr/share/locale/uk/LC_MESSAGES/kio6-win7.mo || true
    else warn "$(L "KIO $(v libkf6kio-dev) не та версія, для якої зроблені зміни (6.24.0): діалоги файлів лишаться звичайні" "KIO $(v libkf6kio-dev) is not the version the changes are made for (6.24.0): file dialogs stay as they are")"; fi
    if [ "$(v dolphin)" = 25.12.3 ]; then
        fetch dolphin-aero "$WORK/src/dolphin-aero"
        cmake_install "$WORK/src/dolphin-aero" "$WORK/src/dolphin-aero/build" -DBUILD_TESTING=OFF
    else warn "$(L "Dolphin $(v dolphin) не та версія (25.12.3): Провідник буде звичайним Dolphin" "Dolphin $(v dolphin) is not the expected version (25.12.3): Explorer stays plain Dolphin")"; fi
    if [ "$(v qt6-style-kvantum)" = 1.1.5 ]; then
        fetch kvantum "$WORK/src/kvantum"
        cmake_install "$WORK/src/kvantum/Kvantum" "$WORK/src/kvantum/Kvantum/build" -DENABLE_QT5=OFF
    else warn "$(L "Kvantum $(v qt6-style-kvantum) не та версія (1.1.5): меню без лінії-розділювача Windows 7" "Kvantum $(v qt6-style-kvantum) is not the expected version (1.1.5): menus lack the Windows 7 separator line")"; fi
fi
note "$(L "готово" "done")"

# ------------------------------------------------------------------ 4. our programs
step "$(L "Програми Windows 7 (Панель керування, Paint, WMP, гаджети...)" "Windows 7 programs (Control Panel, Paint, WMP, gadgets...)")"
for d in "$REPO"/apps/seven-*/; do
    [ -f "$d/CMakeLists.txt" ] || continue
    n=$(basename "$d")
    note "$n"
    if [ "$n" = seven-folderthumb ]; then
        cmake_install "$d" "$WORK/build/$n"
    else
        run cmake -S "$d" -B "$WORK/build/$n" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DCMAKE_CXX_FLAGS="-I$WORK/src"
        run cmake --build "$WORK/build/$n" -j"$JOBS"
        run cmake --install "$WORK/build/$n"
    fi
done
L=$HOME/.local
mkdir -p "$L/bin" "$L/lib/sevenplasma" "$L/share/applications" "$L/share/plasma/plasmoids" "$L/share/kwin/scripts" \
         "$HOME/.config/autostart" "$HOME/.config/systemd/user"
install -m 755 "$REPO/apps/seven-snip/seven-snip.py" "$L/lib/sevenplasma/seven-snip.py"
printf '#!/bin/sh\nexec python3 "%s" "$@"\n' "$L/lib/sevenplasma/seven-snip.py" > "$L/bin/seven-snip"; chmod +x "$L/bin/seven-snip"
cp -r "$REPO/data/lib/." "$L/lib/sevenplasma/"
install -m 755 "$REPO"/data/bin/* "$L/bin/"
subst() { sed "s#@HOME@#$HOME#g" "$1" > "$2"; }
for f in "$REPO"/data/applications/*.desktop; do subst "$f" "$L/share/applications/$(basename "$f")"; done
for f in "$REPO"/data/autostart/*.desktop; do subst "$f" "$HOME/.config/autostart/$(basename "$f")"; done
for f in "$REPO"/data/systemd/*.service; do subst "$f" "$HOME/.config/systemd/user/$(basename "$f")"; done
cp -r "$REPO"/apps/plasmoids/* "$L/share/plasma/plasmoids/"
cp -r "$REPO/apps/kwin/sevenkeys" "$L/share/kwin/scripts/"
run systemctl --user daemon-reload
run systemctl --user enable seven-folder-refresh.service || true
# "Open with" of programs that ask through xdg-desktop-portal (Chrome, Telegram, Flatpak...): our Windows 7 dialog
mkdir -p "$L/share/dbus-1/services" "$HOME/.config/xdg-desktop-portal"
printf '[D-BUS Service]\nName=org.freedesktop.impl.portal.desktop.win7\nExec=%s/bin/seven-control --portal\n' "$L" \
    > "$L/share/dbus-1/services/org.freedesktop.impl.portal.desktop.win7.service"
printf '[preferred]\ndefault=kde\norg.freedesktop.impl.portal.Settings=kde;gtk;\norg.freedesktop.impl.portal.Secret=kwallet\norg.freedesktop.impl.portal.AppChooser=seven;kde;\n' \
    > "$HOME/.config/xdg-desktop-portal/kde-portals.conf"
run kbuildsycoca6 || true
note "$(L "готово" "done")"

# ------------------------------------------------------------------ 5. Windows 7 resources from the user's image
step "$(L "Ресурси Windows 7 з вашого образу" "Windows 7 resources from your image")"
if [ -n "$ISO" ]; then
    note "$(L "читаю образ (кілька хвилин)..." "reading the image (a few minutes)...")"
    TREE=$WORK/windows
    rm -rf "$TREE"; mkdir -p "$TREE"
    run 7z e -y -o"$TREE" "$ISO" sources/install.wim
    IDX=$(7z l "$TREE/install.wim" | awk -F' = ' '/^Images = /{n=$2} END{print n}')
    run 7z x -y -ssc- -o"$TREE" "$TREE/install.wim" \
        "$IDX/Windows/System32/*" "-x!$IDX/Windows/System32/DriverStore" "-x!$IDX/Windows/System32/catroot*" \
        "$IDX/Windows/*.exe" "$IDX/Windows/Resources/*" "$IDX/Windows/Fonts/*" "$IDX/Windows/Media/*" "$IDX/Windows/Web/*" \
        "$IDX/Windows/assembly/GAC_MSIL/*" "$IDX/Program Files/*" "$IDX/ProgramData/Microsoft/User Account Pictures/*" \
        "$IDX/Users/Public/Pictures/Sample Pictures/*"
    rm -f "$TREE/install.wim"
    W=$TREE/$IDX
    run python3 "$REPO/extract/seven-extract.py" --tree "$W" "$L/share"
    N=$(python3 -c "import json;print(len(json.load(open('$REPO/extract/recipes.json'))))")
    note "$(L "іконки, картинки, шрифти, звуки, шпалери: $N файлів" "icons, pictures, fonts, sounds, wallpapers: $N files")"
    # the shortcut arrow of desktop icons (imageres.dll 163 / 1793) goes into ATP's desktop plasmoid
    run sudo install -D -m 644 -t /usr/share/plasma/plasmoids/io.gitgud.wackyideas.desktopcontainment/contents/images "$L"/share/sevenplasma/plasmoid-images/shortcut-*.png
    mkdir -p "$HOME/.config/Kvantum"
    for t in Windows7 Windows7Blue Windows7Desktop; do
        run python3 "$REPO/extract/kvbuild.py" "$REPO/kvantum/$t" "$W/Windows/Resources/Themes/Aero/aero.msstyles" \
            "$WORK/atp/main/misc/kvantum/Windows7Aero/Windows7Aero.svg" "$HOME/.config/Kvantum"
    done
    run python3 "$REPO/extract/plymouth-build.py" "$W" "$WORK/atp/main" "$L/share/fonts/Windows 7" "$WORK/plymouth"
    run sudo mkdir -p /usr/share/plymouth/themes/sevenplasma
    run sudo cp "$WORK"/plymouth/*.png "$REPO"/boot/plymouth/sevenplasma.* /usr/share/plymouth/themes/sevenplasma/
    run sudo cp "$L/share/sounds/Windows 7" -r /usr/share/sounds/ 2>/dev/null || true
    run fc-cache -f
    if [ $GAMES -eq 1 ]; then
        note "$(L "ігри Windows 7 (Wine)" "Windows 7 games (Wine)")"
        export WINEPREFIX="$L/share/sevenplasma/wine"
        WINEDLLOVERRIDES="mscoree,mshtml=" run wineboot -i || true   # no Mono/Gecko install prompts
        mkdir -p "$WINEPREFIX/drive_c/Program Files/Microsoft Games"
        cp -r "$W/Program Files/Microsoft Games/." "$WINEPREFIX/drive_c/Program Files/Microsoft Games/"
    fi
    rm -rf "$TREE"
else
    note "$(L "образ не вказано: беру тему Kvantum ATP і вільний шрифт Selawik" "no image given: using ATP's Kvantum theme and the free Selawik font")"
    mkdir -p "$HOME/.config/Kvantum" "$L/share/fonts/Selawik"
    cp -r "$WORK/atp/main/misc/kvantum/Windows7Aero" "$HOME/.config/Kvantum/Windows7"
    mv "$HOME/.config/Kvantum/Windows7/Windows7Aero.svg" "$HOME/.config/Kvantum/Windows7/Windows7.svg" 2>/dev/null || true
    cp "$REPO/kvantum/Windows7/Windows7.kvconfig" "$HOME/.config/Kvantum/Windows7/"
    if run curl -fsSL -o "$WORK/selawik.zip" https://github.com/microsoft/Selawik/releases/download/1.01/Selawik_Release.zip; then
        run unzip -o -j "$WORK/selawik.zip" '*.ttf' -d "$L/share/fonts/Selawik" && run fc-cache -f
    fi
fi
note "$(L "готово" "done")"

# ------------------------------------------------------------------ 6. boot, logon, shutdown
step "$(L "Завантаження, вхід і завершення роботи" "Boot, logon and shutdown")"
B=$REPO/apps/seven-common/boot
run sudo install -D -m 755 "$B/seven-nopass-users" /usr/local/libexec/seven-nopass-users
run sudo install -D -m 755 "$B/seven-sddm-xsetup" /usr/local/libexec/seven-sddm-xsetup
run sudo install -D -m 755 "$B/seven-shutdown-hold" /usr/local/libexec/seven-shutdown-hold
run sudo install -D -m 755 "$B/seven-splash-hold" /usr/local/libexec/seven-splash-hold
run sudo install -D -m 644 "$B/seven-shutdown-hold.conf" /etc/systemd/system/sddm.service.d/seven-shutdown-hold.conf
run sudo install -D -m 644 "$B/seven-min-time.conf" /etc/systemd/system/plymouth-quit.service.d/seven-min-time.conf
run sudo install -D -m 644 "$B/seven-min-time.conf" /etc/systemd/system/plymouth-quit-wait.service.d/seven-min-time.conf
run sudo install -D -m 644 "$B/seven-stop-timeout.conf" /etc/systemd/system.conf.d/seven-stop-timeout.conf
printf '[General]\nDisplayServer=x11\n\n[Theme]\nCurrent=sddm-theme-mod\n\n[X11]\nDisplayCommand=/usr/local/libexec/seven-sddm-xsetup\nServerArguments=-nolisten tcp -background none\n' > "$WORK/zz-sevenplasma.conf"
run sudo install -D -m 644 "$WORK/zz-sevenplasma.conf" /etc/sddm.conf.d/zz-sevenplasma.conf
# the logon screen preselects the last session: make it AeroThemePlasma on X11 (SevenPlasma is X11 only;
# the Wayland session would close at once and bring the logon screen back)
XS=/usr/share/xsessions/aerothemeplasmax11.desktop
if [ -f "$XS" ]; then
    ST=/var/lib/sddm/state.conf
    if sudo test -f "$ST" && sudo grep -q '^Session=' "$ST"; then
        run sudo sed -i "s#^Session=.*#Session=$XS#" "$ST"
    elif sudo test -f "$ST"; then
        run sudo sed -i "/^\[Last\]/a Session=$XS" "$ST"
        sudo grep -q '^Session=' "$ST" || printf '\n[Last]\nSession=%s\n' "$XS" | run sudo tee -a "$ST"
    else
        printf '[Last]\nSession=%s\n' "$XS" | run sudo install -D -m 644 -o sddm -g sddm /dev/stdin "$ST"
    fi
fi
# one continuous shutdown screen: the session's picture stays on X until the end (no Plymouth shutdown screen)
run sudo systemctl mask plymouth-poweroff.service plymouth-reboot.service plymouth-halt.service
if [ -f /usr/share/plymouth/themes/sevenplasma/sevenplasma.plymouth ]; then
    run sudo plymouth-set-default-theme sevenplasma || run sudo update-alternatives --install /usr/share/plymouth/themes/default.plymouth default.plymouth /usr/share/plymouth/themes/sevenplasma/sevenplasma.plymouth 200
    note "$(L "оновлюю initramfs (заставка «Запуск Windows»)..." "updating the initramfs (the \"Starting Windows\" screen)...")"
    run sudo update-initramfs -u
fi
printf '[portal]\nDBusName=org.freedesktop.impl.portal.desktop.win7\nInterfaces=org.freedesktop.impl.portal.AppChooser\nUseIn=KDE\n' \
    | run sudo install -D -m 644 /dev/stdin /usr/share/xdg-desktop-portal/portals/seven.portal
# translations of the shell, Explorer and the file dialogs (gettext domain "sevenplasma"; the Start menu reads it
# through its own plasmoid domain too)
for po in "$REPO"/apps/seven-common/i18n/sevenplasma/*.po; do
    lang=$(basename "$po" .po)
    [ "$lang" = manual ] && continue
    run msgfmt -o "$WORK/sevenplasma-$lang.mo" "$po"
    for dom in sevenplasma plasma_applet_io.gitgud.wackyideas.SevenStart; do
        run sudo install -D -m 644 "$WORK/sevenplasma-$lang.mo" "/usr/share/locale/$lang/LC_MESSAGES/$dom.mo"
    done
done
# the account picture: like a new Windows 7 account, a random one of the standard pictures (when there is none)
PICS=("$L"/share/seven-control/account-pictures/usertile*.png)
AS=/var/lib/AccountsService/users/$USER
if [ -f "${PICS[0]}" ] && ! sudo grep -qs '^Icon=/' "$AS"; then
    PIC=${PICS[RANDOM % ${#PICS[@]}]}
    run sudo install -D -m 644 "$PIC" "/var/lib/AccountsService/icons/$USER"
    if sudo test -f "$AS" && sudo grep -q '^\[User\]' "$AS"; then
        run sudo sed -i "/^\[User\]/a Icon=/var/lib/AccountsService/icons/$USER" "$AS"
    else
        printf '[User]\nIcon=/var/lib/AccountsService/icons/%s\n' "$USER" | run sudo install -D -m 600 /dev/stdin "$AS"
    fi
fi
run sudo systemctl daemon-reload
note "$(L "готово" "done")"

# ------------------------------------------------------------------ 7. settings
# The global theme, effects and Plasma settings need the running session: seven-firstrun does them at the
# next login (or right now when install.sh runs inside Plasma). AeroThemePlasma's setup wizard and the KDE
# Welcome Center are not needed then.
step "$(L "Налаштування Plasma" "Plasma settings")"
kw() { kwriteconfig6 --file "$1" --group "$2" --key "$3" "$4" >> "$LOG" 2>&1; }
kw aerothemeplasmarc OOTB wizardRun true
[ -n "$PLASMA" ] && kw plasma-welcomerc General LastSeenVersion "$PLASMA"
rm -f "$HOME/.config/sevenplasma-firstrun.done"
printf '[Desktop Entry]\nType=Application\nName=SevenPlasma\nExec=%s/bin/seven-firstrun\nX-KDE-autostart-after=panel\nX-KDE-autostart-phase=2\nNoDisplay=true\n' "$L" \
    > "$HOME/.config/autostart/seven-firstrun.desktop"
# window decoration exceptions (smodrc): Gadgets, file dialogs, Explorer and Control Panel frames, dialogs
cp "$REPO/data/smodrc" "$HOME/.config/smodrc"
mkdir -p "$HOME/.config/environment.d"
echo GTK_USE_PORTAL=1 > "$HOME/.config/environment.d/60-sevenplasma.conf"
# default programs
for m in text/plain text/x-log; do run xdg-mime default seven-notepad.desktop $m; done
for m in image/jpeg image/png image/bmp image/gif image/tiff image/webp; do run xdg-mime default seven-photoviewer.desktop $m; done
for m in video/mp4 video/x-matroska video/x-msvideo video/webm; do run xdg-mime default seven-wmp.desktop $m; done
for m in font/ttf font/otf application/x-font-ttf; do run xdg-mime default seven-fontview.desktop $m; done
if qdbus6 org.kde.plasmashell /PlasmaShell >/dev/null 2>&1; then
    note "$(L "застосовую тему зараз" "applying the theme now")"
    run "$L/bin/seven-firstrun" --now
else
    note "$(L "тема застосується під час наступного входу" "the theme is applied at the next login")"
fi
note "$(L "готово" "done")"

# ------------------------------------------------------------------ 8. done
step "$(L "Готово" "Done")"
echo "$(L "      Перезавантажте комп'ютер. На екрані входу вже вибрано сеанс «AeroThemePlasma (X11)»." "      Restart the computer. The logon screen already has the \"AeroThemePlasma (X11)\" session selected.")"
echo "$(L "      Видалення: ./uninstall.sh" "      To remove: ./uninstall.sh")"
trap - ERR
