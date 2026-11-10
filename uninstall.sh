#!/usr/bin/env bash
# Remove SevenPlasma's own files and settings. AeroThemePlasma and the patched Dolphin / KIO / Kvantum stay
# installed; reinstall the distribution packages to get the originals back:
#   sudo apt install --reinstall dolphin libkf6kiocore6 libkf6kiowidgets6 libkf6kiofilewidgets6 libkf6kiogui6 qt6-style-kvantum
set -u
L=$HOME/.local
case "${LANGUAGE:-${LC_ALL:-${LANG:-}}}" in uk*) UK=1 ;; *) UK=0 ;; esac
T() { if [ "$UK" = 1 ]; then printf '%s' "$1"; else printf '%s' "$2"; fi; }
echo "$(T "Видаляю програми й ресурси SevenPlasma з $L ..." "Removing SevenPlasma programs and resources from $L ...")"
rm -f "$HOME/.config/sevenplasma-firstrun.done" "$L"/bin/seven-* "$L"/share/applications/seven-*.desktop "$L"/share/applications/seven_*.desktop "$HOME"/.config/autostart/seven-*.desktop
rm -rf "$L"/share/seven-* "$L"/lib/sevenplasma "$L"/share/sevenplasma "$L"/share/kwin/scripts/sevenkeys
rm -rf "$L"/share/plasma/plasmoids/io.gitgud.wackyideas.*
find "$L/share/icons/hicolor" -name 'seven-*' -delete 2>/dev/null
rm -rf "$L/share/fonts/Windows 7" "$L/share/sounds/Windows 7" "$L/share/wallpapers/Windows7" "$HOME/.config/Kvantum/Windows7"*
systemctl --user disable --now seven-folder-refresh.service seven-soundfx.service 2>/dev/null
rm -f "$HOME"/.config/systemd/user/seven-*.service "$HOME/.config/environment.d/60-sevenplasma.conf"
kwriteconfig6 --file kdeglobals --group KDE --key widgetStyle Breeze
echo "$(T "Системні частини (потрібен пароль):" "System parts (the password is needed):")"
sudo rm -f /usr/local/libexec/seven-* /etc/systemd/system/sddm.service.d/seven-shutdown-hold.conf \
    /etc/systemd/system/plymouth-quit.service.d/seven-min-time.conf /etc/systemd/system/plymouth-quit-wait.service.d/seven-min-time.conf /etc/systemd/system.conf.d/seven-stop-timeout.conf \
    /etc/sddm.conf.d/zz-sevenplasma.conf
sudo systemctl unmask plymouth-poweroff.service plymouth-reboot.service plymouth-halt.service
if [ -d /usr/share/plymouth/themes/sevenplasma ]; then
    sudo rm -rf /usr/share/plymouth/themes/sevenplasma
    sudo plymouth-set-default-theme bgrt 2>/dev/null || true
    sudo update-initramfs -u
fi
sudo systemctl daemon-reload
fc-cache -f >/dev/null; kbuildsycoca6 >/dev/null 2>&1
echo "$(T "Готово. Вийдіть із системи й увійдіть знову." "Done. Log out and log in again.")"
