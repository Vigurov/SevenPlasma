#!/bin/sh
# Refresh apps/ from the development folders (~/src/win7-*): code only, then rename win7- -> seven- and drop
# personal paths, as for the first import. Run from anywhere; DEV defaults to ~/src.
set -e
DEV=${DEV:-$HOME/src}
R=$(cd "$(dirname "$0")/.." && pwd)
for d in bluetooth-plasmoid calc ccc common control devmgr folderthumb gadgets igfx nvidia paint photoviewer snip system taskmgr wmp; do
    [ -d "$DEV/win7-$d" ] || continue
    rsync -a --delete --exclude='build*/' --exclude='*.bak*' --exclude='*.before*' --exclude='*.dll' --exclude='*.exe' \
        --exclude='*.ico' --exclude='*.png' --exclude='*.gif' --exclude='*.jpg' --exclude='*.wav' --exclude='*.ttf' \
        --exclude='__pycache__' --exclude='install*.sh' --exclude='fix-*.sh' --exclude='tools/dll/' --exclude='iso/' \
        --exclude='win7-weather/' --exclude='snap-aero-cursors/' --exclude='steam-disk-c.sh' --exclude='res.qrc' \
        --exclude='longer-starting.sh' --exclude='apply-*.sh' --exclude='undo-boot.sh' --exclude='hide-grub-menu.sh' \
        --exclude='ASSETS.txt' --exclude='apply.log' --exclude='strings.json' --exclude='win7winrar.h' --exclude='win7zip.h' --exclude='res/' "$DEV/win7-$d/" "$R/apps/seven-$d/"
done
cd "$R/apps"
# file names
find . -depth \( -name '*win7-*' -o -name '*win7folderthumbnail*' -o -name 'win7runner*' \) | while read f; do
    n=$(dirname "$f")/$(basename "$f" | sed -e 's/win7-/seven-/g' -e 's/win7folderthumbnail/sevenfolderthumbnail/' -e 's/win7runner/sevenrunner/')
    mv "$f" "$n"
done
# contents
grep -rlI -e 'win7-' -e 'win7_' -e 'org\.win7\.' -e '/win7/' -e 'win7folderthumbnail' -e 'win7runner' -e "/home/$USER" . | xargs -r sed -i \
    -e 's/org\.win7\./org.sevenplasma./g' -e 's#/win7/#/sevenplasma/#g' -e 's/win7folderthumbnail/sevenfolderthumbnail/g' \
    -e 's/win7runner/sevenrunner/g' -e 's/win7-/seven-/g' -e 's/win7_/seven_/g' \
    -e 's#QStringLiteral("/home/[^/"]*/\.local/bin/#QStringLiteral("#g'
grep -rnI "/home/$USER" . && { echo "personal paths left (see above)"; exit 1; } || true
python3 "$R/tools/fix-folderthumb.py"
python3 "$R/tools/fix-gadgets.py"
echo "apps/ refreshed"
