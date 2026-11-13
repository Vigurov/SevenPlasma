#!/usr/bin/env bash
# SevenPlasma - the Windows 7 experience for KDE Plasma 6, based on AeroThemePlasma.
#
#   ./install.sh [--iso Windows7.iso] [--no-games] [--no-forks] [--yes]
#
# --iso FILE   your own Windows 7 image: icons, sounds, fonts, wallpapers, the boot animation, the games
#              and the pictures of the programs are taken from it on your machine (nothing of Microsoft's
#              is part of SevenPlasma). Without it the theme still installs, with free replacements.
# --no-games   do not install the Windows 7 games (they need Wine)
# --no-forks   do not build the patched Dolphin / KIO / Kvantum (Explorer look, Windows 7 file dialogs)
# --boot-splash  Arch: also turn on the "Starting Windows" boot screen (adds Plymouth to the initramfs and
#              "quiet splash" to the kernel options of GRUB or systemd-boot; uninstall.sh puts them back)
# --from-source  build everything here even when a prebuilt archive for this system is available
# --pack FILE  (for the build servers) build, then pack what was built into FILE (a prebuilt archive) and stop
#
# Tested on Kubuntu 26.04 (Plasma 6.6, X11 session), Arch Linux and Fedora 45 KDE (Plasma 6.8, Wayland session).
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
BOOTSPLASH=0
SOURCE=0
PACK=""
YES=0
JOBS=$(nproc)
# the Plasma settings SevenPlasma changes (kept by the first install, put back by uninstall.sh)
USER_CONFIG=(.config/kdeglobals .config/kwinrc .config/kwinrulesrc .config/plasmarc .config/ksplashrc .config/kcminputrc
    .config/kscreenlockerrc .config/ksmserverrc .config/kglobalshortcutsrc .config/khotkeysrc .config/dolphinrc
    .config/plasma-org.kde.plasma.desktop-appletsrc .config/plasma-io.gitgud.wackyideas.desktop-appletsrc .config/plasmashellrc
    .config/plasma_workspace.notifyrc .config/plasmanotifyrc .config/kactivitymanagerd-statsrc .config/kicker-extra-favoritesrc
    .config/aerothemeplasmarc .config/plasma-welcomerc .config/smodrc .config/breezerc .config/mimeapps.list .config/kded6rc
    .config/systemsettingsrc .config/kxkbrc .config/plasma-localerc .config/kiorc .config/kaccessrc .config/klaunchrc
    .config/kcmfonts .config/powerdevilrc .config/baloofilerc .config/kdedefaults .config/gtk-3.0 .config/gtk-4.0
    .config/gtkrc .config/gtkrc-2.0 .config/xsettingsd/xsettingsd.conf .config/Kvantum/kvantum.kvconfig .gtkrc-2.0
    .config/xdg-desktop-portal/kde-portals.conf .config/environment.d/60-sevenplasma.conf .local/share/kactivitymanagerd)

while [ $# -gt 0 ]; do
    case "$1" in
        --iso) ISO=$(readlink -f "$2"); shift 2 ;;
        --no-games) GAMES=0; shift ;;
        --no-forks) FORKS=0; shift ;;
        --boot-splash) BOOTSPLASH=1; shift ;;
        --from-source) SOURCE=1; shift ;;
        --pack) PACK=$(readlink -f "$2"); shift 2 ;;
        --yes|-y) YES=1; shift ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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
die() {
    # what this run already put into the system is recorded even when it stops half-way (for uninstall.sh)
    if [ -n "${STAMP:-}" ] && declare -F record_system_files >/dev/null; then record_system_files; fi
    printf "\n$(L 'Помилка' 'Error'): %s\n$(L 'Подробиці' 'Details'): %s\n" "$*" "$LOG"; exit 1
}
trap 'die "$(L "команда завершилася невдало (рядок $LINENO)" "a command failed (line $LINENO)")"' ERR

# ------------------------------------------------------------------ checks
[ "$(id -u)" -ne 0 ] || die "$(L "запускайте від звичайного користувача, не від root (пароль спитають, коли треба)" "run as your normal user, not as root (the password is asked when needed)")"
if command -v apt-get >/dev/null; then PM=apt; elif command -v pacman >/dev/null; then PM=pacman
elif command -v dnf >/dev/null && command -v rpm >/dev/null; then PM=dnf
else die "$(L "потрібна Kubuntu / Ubuntu, Arch Linux або Fedora" "Kubuntu / Ubuntu, Arch Linux or Fedora is needed")"; fi
command -v plasmashell >/dev/null || die "$(L "KDE Plasma не встановлено" "KDE Plasma is not installed")"
# installed packages and their versions (without epoch and release)
installed() {
    case $PM in
        apt) dpkg-query -W -f='${db:Status-Abbrev} ${Package}\n' | awk '$1 == "ii" {print $2}' | sort -u ;;
        pacman) pacman -Qq | sort -u ;;
        dnf) rpm -qa --qf '%{NAME}\n' | sort -u ;;
    esac
}
v() {
    case $PM in
        apt) dpkg-query -W -f='${Version}' "$1" 2>/dev/null ;;
        pacman) pacman -Q "$1" 2>/dev/null | awk '{print $2}' ;;
        dnf) rpm -q --qf '%{VERSION}' "$1" 2>/dev/null | grep -v 'not installed' ;;
    esac | sed 's/^[0-9]*://; s/-.*//'
}
PLASMA=$(v plasma-workspace)
# Plasma 6.6 (Kubuntu 26.04): X11 session, patches/; Plasma 6.8 (Arch, Fedora): Wayland session (6.8 has no X11),
# patches/plasma-6.8/
case "$PLASMA" in
    6.6.*) WAYLAND=0; PD=$REPO/patches ;;
    6.8.*) WAYLAND=1; PD=$REPO/patches/plasma-6.8 ;;
    *) die "$(L "потрібна Plasma 6.6 або 6.8 (знайдено: ${PLASMA:-?})" "Plasma 6.6 or 6.8 is needed (found: ${PLASMA:-?})")" ;;
esac
[ -z "$ISO" ] || [ -f "$ISO" ] || die "$(L "файл образу не знайдено: $ISO" "image file not found: $ISO")"

# Prebuilt archives (built by GitHub for each commit of SevenPlasma, .github/workflows/prebuilt.yml) are made for
# one system exactly: the distribution and the versions of Plasma, KDE Frameworks and Qt (the shell, KWin effects
# and the patched KDE parts use their private interfaces). With one for this system and this commit of the
# repository nothing is compiled here.
libver() { local f; f=$(ls /usr/lib64/lib"$1".so.6.* /usr/lib/lib"$1".so.6.* /usr/lib/*/lib"$1".so.6.* 2>/dev/null | head -1); [ -n "$f" ] && basename "$f" | sed "s/^lib$1\.so\.//"; }
# (Arch and the distributions made from it are rolling: one key, whatever version an image of it states)
OSID=$(. /etc/os-release; case " $ID ${ID_LIKE:-} " in *" arch "*) echo arch-rolling ;; *) echo "${ID}-${VERSION_ID:-rolling}" ;; esac)
KEY="$OSID-plasma$PLASMA-kf$(libver KF6CoreAddons || true)-qt$(libver Qt6Core || true)"
# the last commit that changed what goes into an archive (a commit of the README alone needs no new one)
COMMIT=$(git -C "$REPO" log -1 --format=%H -- apps patches install.sh .github/workflows/prebuilt.yml 2>/dev/null | cut -c1-12 || true)
PREBUILT=""
if [ $SOURCE = 0 ] && [ -z "$PACK" ] && [ $FORKS = 1 ] && [ -n "$COMMIT" ] && git -C "$REPO" diff --quiet HEAD -- apps patches 2>/dev/null; then
    PREBUILT_URL="https://github.com/Vigurov/SevenPlasma/releases/download/prebuilt-$COMMIT/sevenplasma-$KEY.tar.xz"
fi
[ $WAYLAND = 1 ] || [ "${XDG_SESSION_TYPE:-x11}" = x11 ] || warn "$(L "зараз сесія $XDG_SESSION_TYPE; SevenPlasma розрахована на X11 (її можна вибрати на екрані входу)" "this is a $XDG_SESSION_TYPE session; SevenPlasma is made for X11 (choose it on the logon screen)")"

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

# ------------------------------------------------------------------ what was there before (for uninstall.sh)
# The first install keeps the Plasma settings, the list of packages, the boot screen, the logon session, the
# keyboard and account settings; every install records the files it puts into the system. uninstall.sh puts
# everything back from this.
BK=$HOME/.local/share/sevenplasma-backup
# uninstall.sh was run and nobody has logged in since: its pending restore of the Plasma settings would undo
# this install at the next login (and delete the backups). It is cancelled; the backups from before the first
# install stay, so a later uninstall still puts the old settings back.
if [ -f "$HOME/.config/plasma-workspace/env/sevenplasma-restore.sh" ]; then
    rm -f "$HOME/.config/plasma-workspace/env/sevenplasma-restore.sh"
    note "$(L "скасовано відновлення налаштувань після видалення (ще не було входу)" "the settings restore of the earlier uninstall is cancelled (no login since)")"
fi
STAMP=$WORK/run.stamp
: > "$STAMP"
mkdir -p "$BK"
touch "$BK/system-files.txt" "$BK/user-files.txt"
if [ ! -f "$BK/saved" ]; then
    installed > "$BK/packages-before.txt"
    # Plasma settings the theme changes (restored at the first login after uninstall.sh, before Plasma starts)
    : > "$BK/absent.txt"
    keep=()
    for f in "${USER_CONFIG[@]}"; do
        if [ -e "$HOME/$f" ]; then keep+=("$f"); else echo "$f" >> "$BK/absent.txt"; fi
    done
    tar -C "$HOME" -czf "$BK/config.tar.gz" --ignore-failed-read "${keep[@]}" 2>> "$LOG" || true
    printf '%s\n' "${keep[@]}" > "$BK/kept.txt"
    # files under /usr of no package (generated caches...) are never removed by uninstall.sh
    sudo find /usr -xdev \( -type f -o -type l \) 2>/dev/null | sudo python3 "$REPO/tools/sevenplasma-files.py" unowned \
        | gzip > "$BK/usr-unowned-before.txt.gz"
    S=$BK/system; mkdir -p "$S"
    # the boot screen: Ubuntu chooses it with the default.plymouth alternative (auto or manual, and the file)
    update-alternatives --query default.plymouth 2>/dev/null | awk '/^Status:/{print $2} /^Value:/{print $2}' > "$S/plymouth" || true
    # Fedora: the boot screen theme by name; the logon manager (Plasma Login Manager there, SDDM is ours)
    [ $PM = dnf ] && { plymouth-set-default-theme 2>/dev/null > "$S/plymouth-theme" || true; }
    readlink /etc/systemd/system/display-manager.service > "$S/display-manager" 2>/dev/null || true
    # (Fedora: plymouth-set-default-theme also leaves a "plymouthd.confe" behind, its sed -ie)
    for f in /var/lib/sddm/state.conf /etc/default/keyboard /etc/X11/xorg.conf.d/00-keyboard.conf "/var/lib/AccountsService/users/$USER" \
             /etc/plymouth/plymouthd.conf /etc/plymouth/plymouthd.confe; do
        if sudo test -f "$f"; then sudo cat "$f" > "$S/$(echo "$f" | tr / _)"; else echo "$f" >> "$S/absent"; fi
    done
    sudo test -e "/var/lib/AccountsService/icons/$USER" || echo "/var/lib/AccountsService/icons/$USER" >> "$S/absent"
    touch "$BK/saved"
fi
record_system_files() {   # every file this run put under /usr (cmake installs also record their manifests)
    sudo find /usr -xdev -cnewer "$STAMP" \( -type f -o -type l \) 2>/dev/null >> "$BK/system-files.txt" || true
    sort -u -o "$BK/system-files.txt" "$BK/system-files.txt"
}

retry() {   # network steps: up to 3 tries (a dropped connection is not the end of the install)
    local i
    for i in 1 2 3; do
        "$@" >> "$LOG" 2>&1 && return 0
        echo "retry $i: $*" >> "$LOG"; sleep 5
    done
    return 1
}
# pinned upstream versions the patches are made for: name url commit
pin() { awk -v n="$1" '$1 == n {print $2, $3}' "$PD/BASES.txt"; }
fetch() {   # fetch NAME DIR -> checkout of the pinned commit with our patch applied
    [ -z "$PREBUILT" ] || return 0
    local name=$1 dir=$2 url commit
    read -r url commit < <(pin "$name")
    if [ ! -d "$dir/.git" ]; then retry git clone --filter=blob:none "$url" "$dir"; fi
    retry git -C "$dir" fetch --quiet origin
    retry git -C "$dir" checkout --quiet --force "$commit"   # fetches the files it needs
    run git -C "$dir" clean -fdq -e build -e 'build*'
    if [ -s "$PD/$name.patch" ]; then run git -C "$dir" apply --whitespace=nowarn "$PD/$name.patch"; fi
}
# a build folder left by an install from another copy of SevenPlasma cannot be reused (CMake refuses it)
fresh_build() {   # fresh_build SRC BUILD
    if [ -f "$2/CMakeCache.txt" ] && ! grep -qx "CMAKE_HOME_DIRECTORY:INTERNAL=$(cd "$1" && pwd)" "$2/CMakeCache.txt"; then
        rm -rf "$2"
    fi
}
cmake_install() {   # cmake_install SRC BUILD [cmake args...]   -> builds and installs to /usr with sudo
    [ -z "$PREBUILT" ] || return 0
    local src=$1 build=$2; shift 2
    fresh_build "$src" "$build"
    run cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_CXX_FLAGS="-I$WORK/src" "$@"
    mkdir -p "$build/bin/x11"   # the KWin effects link to bin/x11/NAME.so, but nothing creates that folder
    run cmake --build "$build" -j"$JOBS"
    run sudo cmake --install "$build"
    run sudo ldconfig   # new shared libraries are found at once
    { cat "$build/install_manifest.txt"; echo; } >> "$BK/system-files.txt" 2>/dev/null || true   # it has no last newline
    { cat "$build/install_manifest.txt"; echo; } >> "$WORK/built-system.txt" 2>/dev/null || true
}

# --pack: what this run built, as a prebuilt archive for systems like this one (the build servers run this):
#   sys/      the files the cmake installs put under /usr (their manifests) and KIO's translation
#   local/    our programs from ~/.local
#   src/      the parts of AeroThemePlasma's sources the install still reads (Kvantum theme, logon pictures)
#   runtime-packages.txt  the distribution packages all that needs at run time: the owners of the libraries
#             its programs link to, and the packages of our list that are not for building
owner_pkg() {
    case $PM in
        apt) dpkg -S "$1" 2>/dev/null | head -1 | cut -d: -f1 ;;
        pacman) pacman -Qqo "$1" 2>/dev/null ;;
        dnf) rpm -qf --qf '%{NAME}\n' "$1" 2>/dev/null | grep -v ' ' ;;
    esac
}
pack_prebuilt() {
    local P=$WORK/pack f lib
    sudo rm -rf "$P"; mkdir -p "$P/sys" "$P/local" "$P/src/atp-main/misc/kvantum" "$P/src/atp-main/plasma/look-and-feel/authui7/contents"
    sort -u "$WORK/built-system.txt" | while IFS= read -r f; do
        if [ -n "$f" ] && { [ -e "$f" ] || [ -L "$f" ]; }; then sudo cp -a --parents "$f" "$P/sys/"; fi
    done
    sort -u "$WORK/built-user.txt" | while IFS= read -r f; do
        case "$f" in "$HOME/.local/"*) [ -e "$f" ] || [ -L "$f" ] || continue; (cd "$HOME/.local" && cp -a --parents "${f#$HOME/.local/}" "$P/local/") ;; esac
    done || true
    cp -a "$WORK/atp/main/misc/kvantum/Windows7Aero" "$P/src/atp-main/misc/kvantum/"
    cp -a "$WORK/atp/main/plasma/look-and-feel/authui7/contents/images" "$P/src/atp-main/plasma/look-and-feel/authui7/contents/"
    {
        # the libraries of the ELF files (ldd resolves them on this system)
        find "$P/sys" "$P/local" -type f | while IFS= read -r f; do
            head -c 4 "$f" | grep -qa ELF || continue
            ldd "$f" 2>/dev/null | awk '$2 == "=>" && $3 ~ /^\// {print $3}'
        done | sort -u | while IFS= read -r lib; do owner_pkg "$(readlink -f "$lib")" || true; owner_pkg "$lib" || true; done
        # and our lists without what is only for building
        case $PM in apt) printf '%s\n' "${PKGS[@]}" ;; pacman) printf '%s\n' "${APKGS[@]}" ;; dnf) printf '%s\n' "${FPKGS[@]}" ;; esac \
            | grep -Evx '.*-dev|.*-devel|cmake|extra-cmake-modules|ninja|ninja-build|build-essential|base-devel|gcc-c\+\+|make|vulkan-headers|wayland-protocols|plasma-wayland-protocols|wine|gst-.*|gstreamer1-.*'
        printf '%s\n' xz-utils
    } | grep -v '^$' | sort -u > "$P/runtime-packages.txt"
    [ $PM = apt ] || sed -i '/^xz-utils$/d' "$P/runtime-packages.txt"
    [ $PM = apt ] || echo xz >> "$P/runtime-packages.txt"
    echo "$KEY" > "$P/key"
    run sudo tar -C "$P" --owner=0 --group=0 --numeric-owner -cJf "$PACK" .
    sudo chown "$(id -u):$(id -g)" "$PACK"
    note "$(L "готовий архів: $PACK ($KEY)" "prebuilt archive: $PACK ($KEY)")"
}

# ------------------------------------------------------------------ 1. packages
# programs of a running Plasma session can crash while their libraries are replaced; KDE's crash handler then
# crashes as well, again and again, until the memory is full (the first login turns it off for good anyway,
# uninstall.sh turns it back on)
systemctl --user mask --now drkonqi-coredump-launcher.socket >> "$LOG" 2>&1 || true
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
# Arch: AeroThemePlasma's list for Arch and what our programs need
APKGS=(git cmake extra-cmake-modules ninja curl unzip base-devel gettext qt6-virtualkeyboard qt6-multimedia qt6-5compat
       qt6-wayland qt6-svg qt6-tools qt6-declarative plasma-wayland-protocols wayland-protocols vulkan-headers plasma5support
       kvantum sddm sddm-kcm plasma-nm plasma-pa plasma-workspace plasma-desktop libplasma packagekit-qt6 libksysguard
       baloo-widgets polkit-qt6 python-pyqt6 python-pillow 7zip fontconfig freetype2 kio kparts kxmlgui knotifications
       kglobalaccel kwindowsystem kiconthemes kcmutils kitemmodels ktextwidgets kuserfeedback kdoctools plasma-activities-stats
       plymouth xorg-xrandr spectacle)
[ $GAMES -eq 1 ] && [ -n "$ISO" ] && APKGS+=(wine gst-plugins-base gst-plugins-good gst-libav)   # the games' sounds go through GStreamer
# Fedora: the same, by the names of its -devel packages
FPKGS=(git cmake extra-cmake-modules ninja-build gcc-c++ make curl unzip gettext 7zip python3-pillow python3-pyqt6
       qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtsvg-devel qt6-qtmultimedia-devel qt6-qttools-devel
       qt6-qt5compat-devel qt6-qtdeclarative-devel qt6-qtwayland-devel qt6-qtvirtualkeyboard
       plasma-wayland-protocols-devel wayland-protocols-devel wayland-devel vulkan-headers libdrm-devel libepoxy-devel
       plasma5support-devel libplasma-devel plasma-workspace-devel kwin-devel kdecoration-devel plasma-activities-devel
       plasma-activities-stats-devel libksysguard-devel baloo-widgets-devel polkit-qt6-1-devel PackageKit-Qt6-devel
       kf6-kio-devel kf6-kparts-devel kf6-kxmlgui-devel kf6-knotifications-devel kf6-kglobalaccel-devel
       kf6-kwindowsystem-devel kf6-kiconthemes-devel kf6-kcmutils-devel kf6-kitemmodels-devel kf6-ktextwidgets-devel
       kf6-kuserfeedback-devel kf6-kdoctools-devel kf6-baloo-devel kf6-kconfig-devel kf6-kfilemetadata-devel
       kf6-kidletime-devel kf6-kservice-devel kf6-solid-devel kf6-kstatusnotifieritem-devel kf6-kauth-devel
       kf6-kcolorscheme-devel kf6-ki18n-devel kf6-kirigami-devel kf6-ksvg-devel kf6-kcrash-devel kf6-knewstuff-devel
       kf6-knotifyconfig-devel kf6-attica-devel kf6-krunner-devel kf6-kdbusaddons-devel kf6-sonnet-devel
       kf6-kcoreaddons-devel kf6-kwidgetsaddons-devel kf6-kguiaddons-devel kf6-qqc2-desktop-style kf6-kpackage-devel
       kf6-kbookmarks-devel kf6-kcodecs-devel kf6-kcompletion-devel kf6-kitemviews-devel kf6-kjobwidgets-devel
       kf6-kconfigwidgets-devel kf6-karchive-devel kf6-kirigami-addons fontconfig-devel freetype-devel libxslt-devel
       libxml2-devel libxcb-devel libXtst xrandr layer-shell-qt-devel kwayland-devel kf6-kded-devel kf6-kwallet-devel
       kf6-frameworkintegration-devel libacl-devel libmount-devel kvantum sddm plasma-nm plasma-pa spectacle plymouth
       plymouth-plugin-script)
[ $GAMES -eq 1 ] && [ -n "$ISO" ] && FPKGS+=(wine gstreamer1-plugins-base gstreamer1-plugins-good gstreamer1-plugin-libav)
# a prebuilt archive for this system: only the packages its programs need at run time (listed in it by the build
# server), no compilers and -dev packages
# (SEVENPLASMA_PREBUILT=FILE: an archive from the disk, for testing)
if [ -n "${SEVENPLASMA_PREBUILT:-}" ] && [ $SOURCE = 0 ] && [ -z "$PACK" ]; then
    cp "$SEVENPLASMA_PREBUILT" "$WORK/prebuilt.tar.xz"; PREBUILT_URL=file
fi
if [ -n "${PREBUILT_URL:-}" ] && { [ "$PREBUILT_URL" = file ] || curl -fsSL --retry 2 -o "$WORK/prebuilt.tar.xz" "$PREBUILT_URL" 2>> "$LOG"; }; then
    PREBUILT=$WORK/prebuilt
    sudo rm -rf "$PREBUILT"; mkdir -p "$PREBUILT"
    # everything but the system part here (the system part goes straight into / later)
    run tar -C "$PREBUILT" -xJf "$WORK/prebuilt.tar.xz" --exclude=./sys
fi
if [ -n "$PREBUILT" ] && [ "$(cat "$PREBUILT/key" 2>/dev/null)" != "$KEY" ]; then   # made for another system
    warn "$(L "готові файли для іншої системи ($(cat "$PREBUILT/key" 2>/dev/null)): збираю тут" "the prebuilt files are for another system ($(cat "$PREBUILT/key" 2>/dev/null)): building here")"
    sudo rm -rf "$PREBUILT"; PREBUILT=""; rm -f "$WORK/prebuilt.tar.xz"
fi
if [ -n "$PREBUILT" ]; then
    mapfile -t RT < "$PREBUILT/runtime-packages.txt"
    note "$(L "є готові файли для цієї системи ($KEY): компілювати нічого не треба" "prebuilt files for this system ($KEY): nothing to compile")"
    case $PM in apt) PKGS=("${RT[@]}") ;; pacman) APKGS=("${RT[@]}") ;; dnf) FPKGS=("${RT[@]}") ;; esac
    [ $GAMES -eq 1 ] && [ -n "$ISO" ] && case $PM in
        apt) PKGS+=(wine) ;; pacman) APKGS+=(wine gst-plugins-base gst-plugins-good gst-libav) ;;
        dnf) FPKGS+=(wine gstreamer1-plugins-base gstreamer1-plugins-good gstreamer1-plugin-libav) ;; esac
fi
case $PM in
    apt)
        run sudo apt-get -o DPkg::Lock::Timeout=900 update
        run sudo apt-get -o DPkg::Lock::Timeout=900 install -y --no-install-recommends "${PKGS[@]}" ;;
    pacman) run sudo pacman -Syu --noconfirm --needed "${APKGS[@]}" ;;
    dnf) run sudo dnf install -y --setopt=install_weak_deps=False "${FPKGS[@]}" ;;
esac
if [ -n "$PREBUILT" ]; then
    # the files the build put into the system and into ~/.local, recorded for uninstall.sh like built ones
    tar -tJf "$WORK/prebuilt.tar.xz" ./sys | grep -v '/$' | sed 's#^\./sys##' >> "$BK/system-files.txt"
    # with tar straight from the archive: new files get the system's own labels (SELinux: copies would carry the
    # labels of the download folder and the system could no longer load its libraries), the folders already
    # there keep theirs, a running program is replaced and not overwritten
    run sudo tar -C / -xJf "$WORK/prebuilt.tar.xz" --strip-components=2 --no-overwrite-dir \
        --no-same-permissions --no-selinux --no-xattrs ./sys
    rm -f "$WORK/prebuilt.tar.xz"
    mkdir -p "$HOME/.local"
    (cd "$PREBUILT/local" && find . \( -type f -o -type l \) | sed "s#^\.#$HOME/.local#") >> "$BK/user-files.txt"
    cp -a --remove-destination "$PREBUILT/local/." "$HOME/.local/"
    run sudo ldconfig
    rm -rf "$PREBUILT/local"   # (src/ is used until the end)
fi
# the packages installed for SevenPlasma (uninstall.sh offers to remove them again)
installed | comm -13 "$BK/packages-before.txt" - >> "$BK/packages-added.txt"
sort -u -o "$BK/packages-added.txt" "$BK/packages-added.txt"
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
    [ -n "$PREBUILT" ] || [ -d "$WORK/atp/repos/$r/.git" ] || retry git clone --depth 1 "https://gitgud.io/aeroshell/atp/$r" "$WORK/atp/repos/$r"
done
fetch aerothemeplasma "$WORK/atp/main"
if [ $WAYLAND = 0 ] && [ -z "$PREBUILT" ]; then   # (in patches/plasma-6.8 they are part of aerothemeplasma.patch)
    run git -C "$WORK/atp/main" apply --whitespace=nowarn "$REPO/patches/atp-local-overrides.patch"
    run git -C "$WORK/atp/main" apply --whitespace=nowarn "$REPO/patches/atp-sddm.patch"
fi
R=$WORK/atp/repos
# where Plasma keeps its helper programs (the session starts /usr/<libexec>/plasma-dbus-run-session-if-needed):
# /usr/lib on Arch and Ubuntu, /usr/libexec on Fedora
LX=lib; [ $PM = dnf ] && LX=libexec
cmake_install "$R/libplasma" "$R/libplasma/build"
cmake_install "$R/uac-polkit-agent" "$R/uac-polkit-agent/build" -DCMAKE_INSTALL_LIBEXECDIR=$LX -DKDE_INSTALL_LIBEXECDIR=$LX
cmake_install "$R/smod" "$R/smod/build"
[ $WAYLAND = 1 ] || cmake_install "$R/smod" "$R/smod/build-x11" -DBUILD_DECORATION=OFF -DBUILD_EFFECTX11=ON
cmake_install "$R/aeroshell-workspace" "$R/aeroshell-workspace/build"
run sudo update-mime-database /usr/local/share/mime || true
cmake_install "$R/aeroshell-kwin-components" "$R/aeroshell-kwin-components/build" -DKWIN_BUILD_WAYLAND=ON
[ $WAYLAND = 1 ] || cmake_install "$R/aeroshell-kwin-components" "$R/aeroshell-kwin-components/build_x11" -DKWIN_BUILD_WAYLAND=OFF -DKWIN_INSTALL_MISC=OFF
cmake_install "$R/aeroshell-sddm-kcm" "$R/aeroshell-sddm-kcm/build"
cmake_install "$R/aerothemeplasma-icons" "$R/aerothemeplasma-icons/build"
cmake_install "$R/aerothemeplasma-sounds" "$R/aerothemeplasma-sounds/build"
cmake_install "$WORK/atp/main" "$WORK/atp/main/build" -DCMAKE_INSTALL_LIBEXECDIR=$LX -DKDE_INSTALL_LIBEXECDIR=$LX
[ $WAYLAND = 1 ] || cmake_install "$WORK/atp/main" "$WORK/atp/main/build_x11" -DCMAKE_INSTALL_LIBEXECDIR=$LX -DKDE_INSTALL_LIBEXECDIR=$LX -DINSTALL_X11_COMPONENTS=ON
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
fetch libaero-qt "$WORK/src/libaero-qt"
cmake_install "$WORK/src/libaero-qt" "$WORK/src/libaero-qt/build"
KIOPKG=libkf6kio-dev; KVPKG=qt6-style-kvantum
[ $PM = pacman ] && { KIOPKG=kio; KVPKG=kvantum; }
[ $PM = dnf ] && { KIOPKG=kf6-kio-core; KVPKG=kvantum; }
# the versions our patches are made for; Explorer: dolphin-aero (Kubuntu), Dolphin with dolphin-aero's and our
# changes in one patch (Plasma 6.8)
KIOVER=6.24.0; DOLVER=25.12.3; KVVER=1.1.5; DOLSRC=dolphin-aero
[ $WAYLAND = 1 ] && { KIOVER=6.31.0; DOLVER=26.08.3; KVVER=1.1.9; DOLSRC=dolphin; }
# KIO is a part of the system's Frameworks: built only at exactly their version. Dolphin of the same release series
# and Kvantum (they only need Qt and Frameworks) are built at the version of the patch over whatever is installed.
if [ -n "$PREBUILT" ]; then
    note "$(L "з готових файлів" "from the prebuilt files")"
elif [ $FORKS -eq 1 ]; then
    if [ "$(v $KIOPKG)" = "$KIOVER" ]; then
        fetch kio "$WORK/src/kio"
        cmake_install "$WORK/src/kio" "$WORK/src/kio/build" -DBUILD_TESTING=OFF
        run msgfmt -o "$WORK/kio6-win7.mo" "$WORK/src/kio/po-win7/uk.po" && run sudo install -m 644 "$WORK/kio6-win7.mo" /usr/share/locale/uk/LC_MESSAGES/kio6-win7.mo \
            && echo /usr/share/locale/uk/LC_MESSAGES/kio6-win7.mo >> "$WORK/built-system.txt" || true
    else warn "$(L "KIO $(v $KIOPKG) не та версія, для якої зроблені зміни ($KIOVER): діалоги файлів лишаться звичайні" "KIO $(v $KIOPKG) is not the version the changes are made for ($KIOVER): file dialogs stay as they are")"; fi
    if [ "${DOLVER%.*}" = "$(v dolphin | cut -d. -f1,2)" ]; then
        fetch $DOLSRC "$WORK/src/$DOLSRC"
        cmake_install "$WORK/src/$DOLSRC" "$WORK/src/$DOLSRC/build" -DBUILD_TESTING=OFF
    else warn "$(L "Dolphin $(v dolphin) не та версія, для якої зроблені зміни: Провідник буде звичайним Dolphin" "Dolphin $(v dolphin) is not a version the changes are made for: Explorer stays plain Dolphin")"; fi
    if [ "${KVVER%.*}" = "$(v $KVPKG | cut -d. -f1,2)" ]; then
        fetch kvantum "$WORK/src/kvantum"
        cmake_install "$WORK/src/kvantum/Kvantum" "$WORK/src/kvantum/Kvantum/build" -DENABLE_QT5=OFF
    else warn "$(L "Kvantum $(v $KVPKG) не та версія ($KVVER): меню без лінії-розділювача Windows 7" "Kvantum $(v $KVPKG) is not the expected version ($KVVER): menus lack the Windows 7 separator line")"; fi
fi
note "$(L "готово" "done")"

# ------------------------------------------------------------------ 4. our programs
step "$(L "Програми Windows 7 (Панель керування, Paint, WMP, гаджети...)" "Windows 7 programs (Control Panel, Paint, WMP, gadgets...)")"
for d in "$REPO"/apps/seven-*/; do
    [ -f "$d/CMakeLists.txt" ] && [ -z "$PREBUILT" ] || continue
    n=$(basename "$d")
    note "$n"
    if [ "$n" = seven-folderthumb ]; then
        cmake_install "$d" "$WORK/build/$n"
    else
        fresh_build "$d" "$WORK/build/$n"
        run cmake -S "$d" -B "$WORK/build/$n" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DCMAKE_CXX_FLAGS="-I$WORK/src"
        run cmake --build "$WORK/build/$n" -j"$JOBS"
        run cmake --install "$WORK/build/$n"
        { cat "$WORK/build/$n/install_manifest.txt"; echo; } >> "$BK/user-files.txt" 2>/dev/null || true
        { cat "$WORK/build/$n/install_manifest.txt"; echo; } >> "$WORK/built-user.txt" 2>/dev/null || true
    fi
done
[ -z "$PACK" ] || { pack_prebuilt; exit 0; }
L=$HOME/.local
mkdir -p "$L/bin" "$L/lib/sevenplasma" "$L/share/applications" "$L/share/plasma/plasmoids" "$L/share/kwin/scripts" \
         "$HOME/.config/autostart" "$HOME/.config/systemd/user"
install -m 755 "$REPO/apps/seven-snip/seven-snip.py" "$L/lib/sevenplasma/seven-snip.py"
printf '#!/bin/sh\nexec python3 "%s" "$@"\n' "$L/lib/sevenplasma/seven-snip.py" > "$L/bin/seven-snip"; chmod +x "$L/bin/seven-snip"
cp -r "$REPO/data/lib/." "$L/lib/sevenplasma/"
install -m 755 "$REPO"/data/bin/* "$L/bin/"
# our scripts and programs call qdbus6 (Ubuntu, Arch); Fedora names it qdbus-qt6
if ! command -v qdbus6 >/dev/null && command -v qdbus-qt6 >/dev/null; then ln -sfn "$(command -v qdbus-qt6)" "$L/bin/qdbus6"; fi
subst() { sed "s#@HOME@#$HOME#g" "$1" > "$2"; }
for f in "$REPO"/data/applications/*.desktop; do subst "$f" "$L/share/applications/$(basename "$f")"; done
for f in "$REPO"/data/autostart/*.desktop; do subst "$f" "$HOME/.config/autostart/$(basename "$f")"; done
# the "Welcome" screen fades into the wallpaper: its link is renewed at logout too (and once now)
mkdir -p "$HOME/.config/plasma-workspace/shutdown"
printf '#!/bin/sh\nexec "%s/bin/seven-splash-wallpaper"\n' "$L" > "$HOME/.config/plasma-workspace/shutdown/seven-splash-wallpaper.sh"
chmod +x "$HOME/.config/plasma-workspace/shutdown/seven-splash-wallpaper.sh"
"$L/bin/seven-splash-wallpaper" || true
for f in "$REPO"/data/systemd/*.service "$REPO"/data/systemd/*.timer; do subst "$f" "$HOME/.config/systemd/user/$(basename "$f")"; done
cp -r "$REPO"/apps/plasmoids/* "$L/share/plasma/plasmoids/"
cp -r "$REPO/apps/kwin/sevenkeys" "$L/share/kwin/scripts/"
run systemctl --user daemon-reload
run systemctl --user enable seven-folder-refresh.service seven-wu-check.timer || true
# "Open with" of programs that ask through xdg-desktop-portal (Chrome, Telegram, Flatpak...): our Windows 7 dialog
mkdir -p "$L/share/dbus-1/services" "$HOME/.config/xdg-desktop-portal"
printf '[D-BUS Service]\nName=org.freedesktop.impl.portal.desktop.win7\nExec=%s/bin/seven-control --portal\n' "$L" \
    > "$L/share/dbus-1/services/org.freedesktop.impl.portal.desktop.win7.service"
printf '[preferred]\ndefault=kde\norg.freedesktop.impl.portal.Settings=kde;gtk;\norg.freedesktop.impl.portal.Secret=kwallet\norg.freedesktop.impl.portal.AppChooser=seven;kde;\n' \
    > "$HOME/.config/xdg-desktop-portal/kde-portals.conf"
run kbuildsycoca6 || true
note "$(L "готово" "done")"

# ------------------------------------------------------------------ 5. Windows 7 resources from the user's image
# the parts of AeroThemePlasma's sources used from here on (the Kvantum theme, the logon pictures)
ATPMAIN=$WORK/atp/main
[ -z "$PREBUILT" ] || ATPMAIN=$PREBUILT/src/atp-main
step "$(L "Ресурси Windows 7 з вашого образу" "Windows 7 resources from your image")"
if [ -n "$ISO" ]; then
    note "$(L "читаю образ (кілька хвилин)..." "reading the image (a few minutes)...")"
    TREE=$WORK/windows
    rm -rf "$TREE"; mkdir -p "$TREE"
    run 7z e -y -o"$TREE" "$ISO" sources/install.wim
    IDX=$(7z l "$TREE/install.wim" | awk -F' = ' '/^Images = /{n=$2} END{print n}')
    run 7z x -y -ssc- -o"$TREE" "$TREE/install.wim" \
        "$IDX/Windows/System32/*" "-x!$IDX/Windows/System32/DriverStore" "-x!$IDX/Windows/System32/catroot*" \
        "$IDX/Windows/*.exe" "$IDX/Windows/Help/Windows/*" "$IDX/Windows/Resources/*" "$IDX/Windows/Fonts/*" "$IDX/Windows/Media/*" "$IDX/Windows/Web/*" \
        "$IDX/Windows/assembly/GAC_MSIL/*" "$IDX/Program Files/*" "$IDX/ProgramData/Microsoft/User Account Pictures/*" \
        "$IDX/Users/Public/Pictures/Sample Pictures/*"
    rm -f "$TREE/install.wim"
    W=$TREE/$IDX
    run python3 "$REPO/extract/seven-extract.py" --tree "$W" "$L/share"
    run python3 "$REPO/extract/draw-missing.py" "$L/share"   # the pieces Windows 7 has no picture of
    N=$(python3 -c "import json;print(len(json.load(open('$REPO/extract/recipes.json'))))")
    note "$(L "іконки, картинки, шрифти, звуки, шпалери: $N файлів" "icons, pictures, fonts, sounds, wallpapers: $N files")"
    # the shortcut arrow of desktop icons (imageres.dll 163 / 1793) goes into ATP's desktop plasmoid
    run sudo install -D -m 644 -t /usr/share/plasma/plasmoids/io.gitgud.wackyideas.desktopcontainment/contents/images "$L"/share/sevenplasma/plasmoid-images/shortcut-*.png
    mkdir -p "$HOME/.config/Kvantum"
    for t in Windows7 Windows7Blue Windows7Desktop; do
        run python3 "$REPO/extract/kvbuild.py" "$REPO/kvantum/$t" "$W/Windows/Resources/Themes/Aero/aero.msstyles" \
            "$ATPMAIN/misc/kvantum/Windows7Aero/Windows7Aero.svg" "$HOME/.config/Kvantum"
    done
    run python3 "$REPO/extract/plymouth-build.py" "$W" "$ATPMAIN" "$L/share/fonts/Windows 7" "$WORK/plymouth"
    run sudo mkdir -p /usr/share/plymouth/themes/sevenplasma
    run sudo cp "$WORK"/plymouth/*.png "$REPO"/boot/plymouth/sevenplasma.* /usr/share/plymouth/themes/sevenplasma/
    run sudo cp "$L/share/sounds/Windows 7" -r /usr/share/sounds/ 2>/dev/null || true
    run fc-cache -f
    # the vendor panels' own icons come with their Windows drivers (not in the image): the Linux driver's
    for v in nvidia:nvidia-settings ccc:amd igfx:intel; do
        for sz in 16 32 48; do
            t=$L/share/icons/hicolor/${sz}x${sz}/apps/seven-${v%%:*}.png
            src=$(find /usr/share/icons/hicolor/${sz}x${sz}/apps /usr/share/pixmaps -name "${v#*:}*.png" 2>/dev/null | head -1)
            [ -e "$t" ] || [ -z "$src" ] || install -D -m 644 "$src" "$t"
        done
    done
    t=$L/share/icons/hicolor/24x24/apps/seven-cpa-nvidia.png   # its Control Panel item (All Control Panel Items)
    src=$(find /usr/share/icons/hicolor/{24x24,32x32,48x48}/apps /usr/share/pixmaps -name "nvidia-settings*.png" 2>/dev/null | head -1)
    [ -e "$t" ] || [ -z "$src" ] || python3 -c 'import sys; from PIL import Image; Image.open(sys.argv[1]).convert("RGBA").resize((24, 24), Image.LANCZOS).save(sys.argv[2])' "$src" "$t"
    if [ $GAMES -eq 1 ]; then
        note "$(L "ігри Windows 7 (Wine)" "Windows 7 games (Wine)")"
        export WINEPREFIX="$L/share/sevenplasma/wine"
        WINEDLLOVERRIDES="mscoree,mshtml=" run wineboot -i || true   # no Mono/Gecko install prompts
        mkdir -p "$WINEPREFIX/drive_c/Program Files/Microsoft Games"
        cp -r "$W/Program Files/Microsoft Games/." "$WINEPREFIX/drive_c/Program Files/Microsoft Games/"
        run python3 "$REPO/tools/games-json.py" "$W/Program Files/Microsoft Games" "$L/share/seven-control/games.json"
    fi
    rm -rf "$TREE"
else
    note "$(L "образ не вказано: беру тему Kvantum ATP і вільний шрифт Selawik" "no image given: using ATP's Kvantum theme and the free Selawik font")"
    mkdir -p "$HOME/.config/Kvantum" "$L/share/fonts/Selawik"
    cp -r "$ATPMAIN/misc/kvantum/Windows7Aero" "$HOME/.config/Kvantum/Windows7"
    mv "$HOME/.config/Kvantum/Windows7/Windows7Aero.svg" "$HOME/.config/Kvantum/Windows7/Windows7.svg" 2>/dev/null || true
    cp "$REPO/kvantum/Windows7/Windows7.kvconfig" "$HOME/.config/Kvantum/Windows7/"
    if run curl -fsSL -o "$WORK/selawik.zip" https://github.com/microsoft/Selawik/releases/download/1.01/Selawik_Release.zip; then
        run unzip -o -j "$WORK/selawik.zip" '*.ttf' -d "$L/share/fonts/Selawik" && run fc-cache -f
    fi
fi
note "$(L "готово" "done")"

# Arch: Plymouth in the initramfs, "quiet splash" for the kernel, our theme. Every file is kept first (the
# first time only), uninstall.sh puts them back.
boot_splash_arch() {
    local S=$BK/system f e changed=0
    for f in /etc/mkinitcpio.conf /etc/default/grub /etc/plymouth/plymouthd.conf /boot/loader/entries/*.conf /efi/loader/entries/*.conf; do
        [ -f "$f" ] || continue
        e=$S/boot$(echo "$f" | tr / _)
        [ -e "$e" ] || sudo cat "$f" > "$e"
        grep -qxF "$f" "$S/boot-files" 2>/dev/null || echo "$f" >> "$S/boot-files"
    done
    sudo test -f /etc/plymouth/plymouthd.conf || { grep -qxF /etc/plymouth/plymouthd.conf "$S/absent" 2>/dev/null || echo /etc/plymouth/plymouthd.conf >> "$S/absent"; }
    # the hook right after systemd / udev (it must come before the disk hooks)
    if ! grep -qE '^HOOKS=.*\bplymouth\b' /etc/mkinitcpio.conf; then
        run sudo sed -i -E '/^HOOKS=/ s/\b(systemd|udev)\b/\1 plymouth/' /etc/mkinitcpio.conf
    fi
    grep -qE '^HOOKS=.*\bplymouth\b' /etc/mkinitcpio.conf || { warn "$(L "не вдалося додати plymouth у /etc/mkinitcpio.conf" "could not add plymouth to /etc/mkinitcpio.conf")"; return 0; }
    if [ -f /etc/default/grub ] && command -v grub-mkconfig >/dev/null; then
        for o in quiet splash; do
            grep -qE "^GRUB_CMDLINE_LINUX_DEFAULT=.*\b$o\b" /etc/default/grub || { run sudo sed -i -E "s/^(GRUB_CMDLINE_LINUX_DEFAULT=\"[^\"]*)\"/\1 $o\"/" /etc/default/grub; changed=1; }
        done
        [ $changed = 1 ] && run sudo grub-mkconfig -o /boot/grub/grub.cfg
    elif ls /boot/loader/entries/*.conf /efi/loader/entries/*.conf >/dev/null 2>&1; then
        for f in /boot/loader/entries/*.conf /efi/loader/entries/*.conf; do
            [ -f "$f" ] || continue
            for o in quiet splash; do
                grep -qE "^options.*\b$o\b" "$f" || run sudo sed -i -E "s/^(options.*)$/\1 $o/" "$f"
            done
        done
    else
        warn "$(L "завантажувач не знайдено: додайте «quiet splash» до параметрів ядра самі" "no boot loader found: add \"quiet splash\" to the kernel options yourself")"
    fi
    run sudo plymouth-set-default-theme sevenplasma
    note "$(L "оновлюю initramfs (заставка «Запуск Windows»)..." "updating the initramfs (the \"Starting Windows\" screen)...")"
    run sudo mkinitcpio -P
}

# ------------------------------------------------------------------ 6. boot, logon, shutdown
step "$(L "Завантаження, вхід і завершення роботи" "Boot, logon and shutdown")"
B=$REPO/apps/seven-common/boot
run sudo install -D -m 755 "$B/seven-nopass-users" /usr/local/libexec/seven-nopass-users
if [ $WAYLAND = 0 ]; then
    # X11: the logon screen's X setup, the shutdown picture kept on X
    run sudo install -D -m 755 "$B/seven-sddm-xsetup" /usr/local/libexec/seven-sddm-xsetup
    run sudo install -D -m 755 "$B/seven-shutdown-hold" /usr/local/libexec/seven-shutdown-hold
    run sudo install -D -m 644 "$B/seven-shutdown-hold.conf" /etc/systemd/system/sddm.service.d/seven-shutdown-hold.conf
fi
if [ $WAYLAND = 0 ] || [ $BOOTSPLASH = 1 ] || [ $PM = dnf ]; then
    # "Starting Windows" held on screen for a few seconds, like Windows 7, however fast the boot is
    run sudo install -D -m 755 "$B/seven-splash-hold" /usr/local/libexec/seven-splash-hold
    run sudo install -D -m 644 "$B/seven-min-time.conf" /etc/systemd/system/plymouth-quit.service.d/seven-min-time.conf
    run sudo install -D -m 644 "$B/seven-min-time.conf" /etc/systemd/system/plymouth-quit-wait.service.d/seven-min-time.conf
fi
run sudo install -D -m 644 "$B/seven-stop-timeout.conf" /etc/systemd/system.conf.d/seven-stop-timeout.conf
# the logon screen gets the same keyboard layouts as the user, now and whenever they change
run sudo install -D -m 755 "$B/seven-keyboard-sync" /usr/local/libexec/seven-keyboard-sync
for u in seven-keyboard-sync.path seven-keyboard-sync.service; do
    subst "$B/$u" "$WORK/$u"; run sudo install -m 644 "$WORK/$u" /etc/systemd/system/$u
done
run sudo systemctl daemon-reload
run sudo systemctl enable seven-keyboard-sync.path seven-keyboard-sync.service
run sudo systemctl start seven-keyboard-sync.service || true
if [ $WAYLAND = 0 ]; then
    printf '[General]\nDisplayServer=x11\n\n[Theme]\nCurrent=sddm-theme-mod\nCursorTheme=aero-drop\nCursorSize=32\n\n[X11]\nDisplayCommand=/usr/local/libexec/seven-sddm-xsetup\nServerArguments=-nolisten tcp -background none\n' > "$WORK/zz-sevenplasma.conf"
else   # the logon screen on Wayland, in KWin (as Plasma's own logon settings set it up)
    printf '[General]\nDisplayServer=wayland\nGreeterEnvironment=QT_WAYLAND_SHELL_INTEGRATION=layer-shell\n\n[Theme]\nCurrent=sddm-theme-mod\nCursorTheme=aero-drop\nCursorSize=32\n\n[Wayland]\nCompositorCommand=kwin_wayland --drm --no-lockscreen --no-global-shortcuts --locale1\n' > "$WORK/zz-sevenplasma.conf"
    # (Fedora logs on with Plasma Login Manager: SDDM takes its place, uninstall.sh gives it back)
    run sudo systemctl enable --force sddm.service || true
fi
run sudo install -D -m 644 "$WORK/zz-sevenplasma.conf" /etc/sddm.conf.d/zz-sevenplasma.conf
# the logon screen preselects the last session: make it AeroThemePlasma (on Plasma 6.6 the X11 one: our
# parts are X11 there, the Wayland session would close at once and bring the logon screen back)
XS=/usr/share/xsessions/aerothemeplasmax11.desktop
[ $WAYLAND = 1 ] && XS=/usr/share/wayland-sessions/aerothemeplasma.desktop
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
[ $WAYLAND = 1 ] || run sudo systemctl mask plymouth-poweroff.service plymouth-reboot.service plymouth-halt.service
# the "Starting Windows" boot screen (Ubuntu; on Arch Plymouth needs its initramfs hook and the "splash" kernel
# option, which are left to the user: the theme is installed as "sevenplasma")
if [ $PM = apt ] && [ -f /usr/share/plymouth/themes/sevenplasma/sevenplasma.plymouth ]; then
    run sudo plymouth-set-default-theme sevenplasma || run sudo update-alternatives --install /usr/share/plymouth/themes/default.plymouth default.plymouth /usr/share/plymouth/themes/sevenplasma/sevenplasma.plymouth 200
    note "$(L "оновлюю initramfs (заставка «Запуск Windows»)..." "updating the initramfs (the \"Starting Windows\" screen)...")"
    run sudo update-initramfs -u
fi
# Fedora: Plymouth is in the initramfs and "rhgb quiet" on the kernel line already: only the theme changes
if [ $PM = dnf ] && [ -f /usr/share/plymouth/themes/sevenplasma/sevenplasma.plymouth ]; then
    note "$(L "оновлюю initramfs (заставка «Запуск Windows»)..." "updating the initramfs (the \"Starting Windows\" screen)...")"
    run sudo plymouth-set-default-theme -R sevenplasma
fi
if [ $PM = pacman ] && [ -f /usr/share/plymouth/themes/sevenplasma/sevenplasma.plymouth ]; then
    if [ $BOOTSPLASH = 1 ]; then
        boot_splash_arch
    else
        note "$(L "заставку завантаження можна ввімкнути: ./install.sh --boot-splash (змінює налаштування завантаження)" "the boot screen can be turned on with ./install.sh --boot-splash (changes the boot settings)")"
    fi
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
record_system_files
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
printf 'GTK_USE_PORTAL=1\n' > "$HOME/.config/environment.d/60-sevenplasma.conf"
# our programs are in ~/.local/bin: Ubuntu's ~/.profile puts it on the PATH, Arch's does not (the session
# then finds none of them: "Could not find program"). Not in environment.d: startplasma takes the PATH of
# the login shell and overwrites systemd's; Plasma runs plasma-workspace/env/*.sh before it does that.
mkdir -p "$HOME/.config/plasma-workspace/env"
cat > "$HOME/.config/plasma-workspace/env/sevenplasma-path.sh" <<'EOF2'
# SevenPlasma: our programs are in ~/.local/bin
case ":$PATH:" in *":$HOME/.local/bin:"*) ;; *) export PATH="$HOME/.local/bin:$PATH" ;; esac
EOF2
# default programs
for m in text/plain text/x-log; do run xdg-mime default seven-notepad.desktop $m || true; done
for m in image/jpeg image/png image/bmp image/gif image/tiff image/webp; do run xdg-mime default seven-photoviewer.desktop $m || true; done
for m in video/mp4 video/x-matroska video/x-msvideo video/webm; do run xdg-mime default seven-wmp.desktop $m || true; done
for m in font/ttf font/otf application/x-font-ttf; do run xdg-mime default seven-fontview.desktop $m || true; done
# right now only inside the AeroThemePlasma session: in plain Plasma the taskbar, Start menu and wallpaper
# settings would go to the other shell's layout
PS=$(pgrep -u "$USER" -x plasmashell | head -1 || true)   # none when installed from a console
if [ -n "$PS" ] && tr '\0' '\n' < "/proc/$PS/environ" 2>/dev/null | grep -qx 'PLASMA_DEFAULT_SHELL=io.gitgud.wackyideas.desktop'; then
    note "$(L "застосовую тему зараз" "applying the theme now")"
    run "$L/bin/seven-firstrun" --now
else
    note "$(L "тема застосується під час наступного входу" "the theme is applied at the next login")"
fi
note "$(L "готово" "done")"

[ -z "$PREBUILT" ] || sudo rm -rf "$PREBUILT"

# ------------------------------------------------------------------ 8. done
step "$(L "Готово" "Done")"
if [ $WAYLAND = 1 ]; then SN="AeroThemePlasma"; else SN="AeroThemePlasma (X11)"; fi
echo "$(L "      Перезавантажте комп'ютер. На екрані входу вже вибрано сеанс «$SN»." "      Restart the computer. The logon screen already has the \"$SN\" session selected.")"
echo "$(L "      Видалення: ./uninstall.sh" "      To remove: ./uninstall.sh")"
trap - ERR
