#!/usr/bin/env bash
# Remove SevenPlasma and put the computer back the way it was before ./install.sh:
#   - SevenPlasma's programs, resources, services and settings files are removed;
#   - the files AeroThemePlasma and the patched Dolphin / KIO / Kvantum / libplasma put into the system are
#     removed, and the distribution packages they replaced are reinstalled (the internet is needed);
#   - the boot screen, the logon session, the keyboard and account settings are put back;
#   - the Plasma settings saved by the first install come back at the next login (before Plasma starts);
#   - the packages installed for building SevenPlasma are removed if you agree.
#
#   ./uninstall.sh [--yes]
set -u
YES=0
[ "${1:-}" = --yes ] || [ "${1:-}" = -y ] && YES=1
L=$HOME/.local
BK=$L/share/sevenplasma-backup
LOG=${XDG_CACHE_HOME:-$HOME/.cache}/sevenplasma-uninstall.log
mkdir -p "$(dirname "$LOG")"; : > "$LOG"
case "${LANGUAGE:-${LC_ALL:-${LANG:-}}}" in uk*) UK=1 ;; *) UK=0 ;; esac
T() { if [ "$UK" = 1 ]; then printf '%s' "$1"; else printf '%s' "$2"; fi; }
ask() {   # ask "question" -> 0 for yes (the default)
    [ $YES -eq 1 ] && return 0
    read -r -p "$1 $(T "[Т/н] " "[Y/n] ")" a
    case "$a" in ""|[TtYyТт]*) return 0 ;; *) return 1 ;; esac
}
[ "$(id -u)" -ne 0 ] || { echo "$(T "запускайте від звичайного користувача" "run as your normal user")"; exit 1; }
if command -v apt-get >/dev/null; then PM=apt; elif command -v pacman >/dev/null; then PM=pacman; else PM=dnf; fi
installed() {
    case $PM in
        apt) dpkg-query -W -f='${db:Status-Abbrev} ${Package}\n' 2>/dev/null | awk '$1 == "ii" {print $2}' | sort -u ;;
        pacman) pacman -Qq | sort -u ;;
        dnf) rpm -qa --qf '%{NAME}\n' | sort -u ;;
    esac
}

echo "$(T "SevenPlasma буде видалено, а система повернеться до стану перед встановленням." "SevenPlasma will be removed and the system put back the way it was before the install.")"
echo "$(T "Журнал: $LOG" "Log: $LOG")"
if [ ! -f "$BK/saved" ]; then
    echo "$(T "Увага: немає збереженого стану (SevenPlasma встановлено старою версією install.sh). Будуть видалені лише її файли; налаштування Plasma лишаться." \
              "Note: no saved state (SevenPlasma was installed by an older install.sh). Only its files are removed; the Plasma settings stay.")"
fi
ask "$(T "Продовжити?" "Continue?")" || exit 0
sudo -n true 2>/dev/null || sudo -v || exit 1

# ------------------------------------------------------------------ the user's SevenPlasma files
echo "$(T "Програми й ресурси SevenPlasma..." "SevenPlasma programs and resources...")"
systemctl --user disable --now seven-folder-refresh.service seven-soundfx.service seven-wu-check.timer seven-wu-check.service 2>/dev/null
if [ -f "$BK/user-files.txt" ]; then   # what our programs' cmake installs put into ~/.local
    while IFS= read -r f; do case "$f" in "$L"/*) rm -f "$f" ;; esac; done < "$BK/user-files.txt"
fi
# KDE's background helpers the first login turned off (marked autostart overrides, push service)
grep -l '^X-SevenPlasma=off' "$HOME"/.config/autostart/*.desktop 2>/dev/null | while read -r f; do rm -f "$f"; done
[ "$(readlink "$HOME/.config/systemd/user/kunifiedpush-distributor.service" 2>/dev/null)" = /dev/null ] && \
    systemctl --user unmask kunifiedpush-distributor.service >/dev/null 2>&1 || true
systemctl --user unmask drkonqi-coredump-launcher.socket >/dev/null 2>&1 && systemctl --user start drkonqi-coredump-launcher.socket >/dev/null 2>&1 || true
rm -f "$HOME/.config/sevenplasma-firstrun.done" "$L"/bin/seven-* "$L"/bin/systemsettings "$L"/bin/kcmshell6 "$L"/bin/kinfocenter "$L"/bin/khelpcenter "$L"/share/applications/seven-*.desktop "$L"/share/applications/seven_*.desktop "$HOME"/.config/autostart/seven-*.desktop "$HOME"/.config/plasma-workspace/shutdown/seven-splash-wallpaper.sh "$HOME"/.config/plasma-workspace/env/sevenplasma-path.sh "$HOME"/.config/autostart/org.kde.discover.notifier.desktop
[ -L "$L/bin/qdbus6" ] && [ "$(readlink "$L/bin/qdbus6")" = "$(command -v qdbus-qt6 2>/dev/null)" ] && rm -f "$L/bin/qdbus6"
rm -rf "$L"/share/seven-* "$L"/lib/sevenplasma "$L"/share/sevenplasma "$L"/share/kwin/scripts/sevenkeys
rm -rf "$L"/share/plasma/plasmoids/io.gitgud.wackyideas.*
find "$L/share/icons/hicolor" -name 'seven-*' -delete 2>/dev/null
rm -rf "$L/share/fonts/Windows 7" "$L/share/fonts/Selawik" "$L/share/sounds/Windows 7" "$L/share/wallpapers/Windows7" "$HOME/.config/Kvantum/Windows7"*
rm -f "$HOME"/.config/systemd/user/seven-*.service "$HOME"/.config/systemd/user/seven-*.timer "$HOME/.config/seven-wu-trayrc" "$HOME/.config/xdg-desktop-portal/kde-portals.conf" "$L"/share/dbus-1/services/org.freedesktop.impl.portal.desktop.win7.service "$HOME/.config/environment.d/60-sevenplasma.conf"
# the Recycle Bin icon seven-firstrun put on the desktop
DESK=$(xdg-user-dir DESKTOP 2>/dev/null); rm -f "${DESK:-$HOME/Desktop}/seven-trash.desktop"
# default programs that pointed to our programs
[ -f "$HOME/.config/mimeapps.list" ] && sed -i '/=seven-[a-z]*\.desktop;\{0,1\}$/d; s/seven-[a-z]*\.desktop;//g' "$HOME/.config/mimeapps.list"

# ------------------------------------------------------------------ system: our files
echo "$(T "Системні частини SevenPlasma..." "SevenPlasma system parts...")"
sudo systemctl disable --now seven-keyboard-sync.path seven-keyboard-sync.service 2>/dev/null || true
sudo rm -f /usr/local/libexec/seven-* /etc/systemd/system/sddm.service.d/seven-shutdown-hold.conf \
    /etc/systemd/system/plymouth-quit.service.d/seven-min-time.conf /etc/systemd/system/plymouth-quit-wait.service.d/seven-min-time.conf /etc/systemd/system.conf.d/seven-stop-timeout.conf \
    /etc/sddm.conf.d/zz-sevenplasma.conf /etc/systemd/system/seven-keyboard-sync.path /etc/systemd/system/seven-keyboard-sync.service /usr/share/xdg-desktop-portal/portals/seven.portal /usr/share/locale/*/LC_MESSAGES/sevenplasma.mo /usr/share/locale/*/LC_MESSAGES/plasma_applet_io.gitgud.wackyideas.SevenStart.mo
sudo systemctl unmask plymouth-poweroff.service plymouth-reboot.service plymouth-halt.service 2>/dev/null
sudo rm -rf /usr/share/plymouth/themes/sevenplasma

# ------------------------------------------------------------------ system: AeroThemePlasma, patched KDE parts
PKGS=()
if [ -s "$BK/system-files.txt" ]; then
    echo "$(T "Файли AeroThemePlasma і змінених Dolphin / KIO / Kvantum / libplasma..." "Files of AeroThemePlasma and the patched Dolphin / KIO / Kvantum / libplasma...")"
    # A file that belongs to a distribution package was replaced: the package is reinstalled. A file of no
    # package is ours: removed. (Packages installed only for SevenPlasma are dealt with below.)
    mapfile -t PKGS < <(sudo python3 "$(dirname "$0")/tools/sevenplasma-files.py" remove "$BK/system-files.txt" \
                                "$BK/packages-before.txt" "$BK/usr-unowned-before.txt.gz")
    sudo ldconfig
    sudo update-mime-database /usr/local/share/mime >/dev/null 2>&1 || true
fi
if [ ${#PKGS[@]} -gt 0 ]; then
    echo "$(T "Перевстановлюю оригінальні пакети (${#PKGS[@]}): потрібен інтернет..." "Reinstalling the original packages (${#PKGS[@]}): the internet is needed...")"
    case $PM in
        apt) sudo NEEDRESTART_SUSPEND=1 apt-get -q -o DPkg::Lock::Timeout=900 install -y --reinstall "${PKGS[@]}" >> "$LOG" 2>&1 ;;
        pacman) sudo pacman -S --noconfirm "${PKGS[@]}" >> "$LOG" 2>&1 ;;
        dnf) sudo dnf reinstall -y "${PKGS[@]}" >> "$LOG" 2>&1 ;;
    esac
    if [ $? -ne 0 ]; then
        echo "$(T "Не вдалося перевстановити пакети. Повторіть пізніше:" "Could not reinstall the packages. Try again later:")"
        case $PM in
            apt) echo "  sudo apt-get install --reinstall ${PKGS[*]}" ;;
            pacman) echo "  sudo pacman -S ${PKGS[*]}" ;;
            dnf) echo "  sudo dnf reinstall ${PKGS[*]}" ;;
        esac
    fi
fi

# ------------------------------------------------------------------ system: settings as before
S=$BK/system
if [ -d "$S" ]; then
    echo "$(T "Заставка завантаження, екран входу, клавіатура, обліковий запис..." "Boot screen, logon screen, keyboard, account...")"
    for f in /var/lib/sddm/state.conf /etc/default/keyboard /etc/X11/xorg.conf.d/00-keyboard.conf "/var/lib/AccountsService/users/$USER" \
             /etc/plymouth/plymouthd.conf /etc/plymouth/plymouthd.confe; do
        c=$S/$(echo "$f" | tr / _)
        if [ -f "$c" ]; then
            sudo install -D -m "$(sudo stat -c %a "$f" 2>/dev/null || echo 644)" "$c" "$f"
        elif grep -qxF "$f" "$S/absent" 2>/dev/null; then
            sudo rm -f "$f"
        fi
    done
    sudo chown sddm:sddm /var/lib/sddm/state.conf 2>/dev/null || true
    grep -qxF "/var/lib/AccountsService/icons/$USER" "$S/absent" 2>/dev/null && sudo rm -f "/var/lib/AccountsService/icons/$USER"
    sudo systemctl try-restart accounts-daemon.service 2>/dev/null || true
    # the boot screen as before: our alternative goes, a manual choice is chosen again
    # the logon manager from before (Fedora: Plasma Login Manager)
    dm=$(cat "$S/display-manager" 2>/dev/null)
    if [ -n "$dm" ] && [ "$(basename "$dm")" != sddm.service ] && [ -f "$dm" ]; then
        sudo systemctl enable --force "$(basename "$dm")" >> "$LOG" 2>&1 || true
    fi
    sudo update-alternatives --remove default.plymouth /usr/share/plymouth/themes/sevenplasma/sevenplasma.plymouth >/dev/null 2>&1 || true
    mode=$(sed -n 1p "$S/plymouth" 2>/dev/null); old=$(sed -n 2p "$S/plymouth" 2>/dev/null)
    if [ "$mode" = manual ] && [ -f "$old" ]; then sudo update-alternatives --set default.plymouth "$old" >/dev/null 2>&1 || true
    else sudo update-alternatives --auto default.plymouth >/dev/null 2>&1 || true; fi
fi
echo "$(T "Оновлюю initramfs (заставка завантаження)..." "Updating the initramfs (boot screen)...")"
[ $PM = apt ] && { sudo update-initramfs -u >> "$LOG" 2>&1 || true; }
# Fedora: plymouthd.conf is back as it was (above): the initramfs gets the boot screen theme from it
[ $PM = dnf ] && { sudo dracut -f >> "$LOG" 2>&1 || true; }
# Arch with --boot-splash: the initramfs hooks, kernel options and Plymouth settings as before
if [ -s "$BK/system/boot-files" ]; then
    while IFS= read -r f; do
        c=$BK/system/boot$(echo "$f" | tr / _)
        [ -f "$c" ] && sudo install -m "$(sudo stat -c %a "$f" 2>/dev/null || echo 644)" "$c" "$f"
    done < "$BK/system/boot-files"
    grep -qxF /etc/plymouth/plymouthd.conf "$BK/system/absent" 2>/dev/null && sudo rm -f /etc/plymouth/plymouthd.conf
    if grep -qxF /etc/default/grub "$BK/system/boot-files" && command -v grub-mkconfig >/dev/null; then
        sudo grub-mkconfig -o /boot/grub/grub.cfg >> "$LOG" 2>&1 || true
    fi
    sudo mkinitcpio -P >> "$LOG" 2>&1 || true
fi
sudo systemctl daemon-reload
sudo fc-cache -f >/dev/null 2>&1

# ------------------------------------------------------------------ packages installed for SevenPlasma
SDDM_UID=$(id -u sddm 2>/dev/null || true)
if [ -s "$BK/packages-added.txt" ]; then
    mapfile -t ADDED < <(installed | comm -12 "$BK/packages-added.txt" -)
    if [ ${#ADDED[@]} -gt 0 ] && ask "$(T "Видалити пакети, встановлені для збирання SevenPlasma (${#ADDED[@]})?" "Remove the packages installed for building SevenPlasma (${#ADDED[@]})?")"; then
        # marked as automatically installed, then removed if nothing else needs them (Ubuntu's autoremove keeps
        # packages another one merely suggests or recommends, like gcc, cmake or plymouth-themes: those of ours go
        # too, never any other package; nothing that something depends on is offered by apt here)
        if [ $PM = apt ]; then
            sudo apt-mark auto "${ADDED[@]}" >/dev/null
            mapfile -t GONE < <(apt-get -s -o APT::AutoRemove::SuggestsImportant=false -o APT::AutoRemove::RecommendsImportant=false autoremove 2>/dev/null \
                | awk '/^Remv /{sub(/:.*/, "", $2); print $2}' | sort -u | comm -12 - "$BK/packages-added.txt")
            if [ ${#GONE[@]} -gt 0 ]; then
                if [ $YES -eq 1 ]; then sudo NEEDRESTART_SUSPEND=1 apt-get purge -y "${GONE[@]}"; else sudo NEEDRESTART_SUSPEND=1 apt-get purge "${GONE[@]}"; fi
            fi
        elif [ $PM = dnf ]; then
            # Fedora: ours become dependencies; those nothing needs any more are removed, in rounds, only ours
            sudo dnf -y mark dependency "${ADDED[@]}" >> "$LOG" 2>&1
            for _ in 1 2 3 4 5 6; do
                mapfile -t GONE < <(dnf repoquery --installed --unneeded --qf '%{name}\n' 2>/dev/null | sort -u | comm -12 - "$BK/packages-added.txt")
                [ ${#GONE[@]} -gt 0 ] || break
                if [ $YES -eq 1 ]; then sudo dnf remove -y --setopt=clean_requirements_on_remove=False "${GONE[@]}" || break
                else sudo dnf remove --setopt=clean_requirements_on_remove=False "${GONE[@]}" || break; fi
            done
        else
            # Arch: ours become dependencies; those nothing needs any more (orphans) are removed with what only
            # they needed (pacman lists them first)
            sudo pacman -D --asdeps "${ADDED[@]}" >/dev/null
            # several rounds: some become orphans only once the packages that needed them are gone; -tt: also
            # those another package merely lists as optional (pacman offers base-devel), only ours anyway
            for _ in 1 2 3 4 5 6; do
                mapfile -t GONE < <(pacman -Qdttq 2>/dev/null | sort -u | comm -12 - "$BK/packages-added.txt")
                [ ${#GONE[@]} -gt 0 ] || break
                if [ $YES -eq 1 ]; then sudo pacman -Rns --noconfirm "${GONE[@]}" || break; else sudo pacman -Rns "${GONE[@]}" || break; fi
            done
        fi
    fi
fi

# SDDM came with SevenPlasma (Fedora) and is gone again: its home (caches) and logind's settings of its account
if grep -qx sddm "$BK/packages-added.txt" 2>/dev/null && ! installed | grep -qx sddm; then
    sudo rm -rf /var/lib/sddm
    [ -n "$SDDM_UID" ] && sudo rm -rf "/etc/systemd/system.control/user@$SDDM_UID.service.d" "/etc/systemd/system.control/user-$SDDM_UID.slice.d"
fi

# ------------------------------------------------------------------ the Plasma settings
# Plasma saves its settings when the session ends, so they are put back at the next login instead, by a
# script startplasma runs before anything of Plasma starts. The script removes itself and the backup.
if [ -f "$BK/config.tar.gz" ] || [ -s "$BK/absent.txt" ]; then
    mkdir -p "$HOME/.config/plasma-workspace/env"
    cat > "$HOME/.config/plasma-workspace/env/sevenplasma-restore.sh" <<EOF
# SevenPlasma uninstall: put back the Plasma settings from before the install (runs once)
(
    B="$BK"
    while IFS= read -r f; do [ -n "\$f" ] && rm -rf "\$HOME/\$f"; done < "\$B/absent.txt"
    if [ -f "\$B/config.tar.gz" ]; then
        while IFS= read -r f; do [ -n "\$f" ] && rm -rf "\$HOME/\$f"; done < "\$B/kept.txt"
        tar -C "\$HOME" -xzf "\$B/config.tar.gz"
    fi
    rm -rf "\$B" "\$HOME/.cache/plasmashell" "\$HOME"/.cache/ksycoca6_* "\$HOME/.cache/plasma_theme_"*
    rm -f "\$HOME/.config/plasma-workspace/env/sevenplasma-restore.sh"
) 2>/dev/null
EOF
else
    rm -rf "$BK"
fi
kwriteconfig6 --file kdeglobals --group KDE --key widgetStyle Breeze 2>/dev/null
kbuildsycoca6 >/dev/null 2>&1; fc-cache -f >/dev/null 2>&1

echo
echo "$(T "Готово. Перезавантажте комп'ютер: на екрані входу буде ваш попередній сеанс, а налаштування Plasma повернуться під час входу." \
          "Done. Restart the computer: the logon screen offers your previous session, and the Plasma settings come back when you log in.")"
