#!/bin/bash
# Regenerate patches/*.patch from the development checkouts (uncommitted changes on top of the pinned
# upstream commits, recorded in patches/BASES.txt). Pictures are left out (Windows resources are extracted on
# the user's machine); names are renamed win7- -> seven- like apps/.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
P=$R/patches
ATP=${ATP:-$HOME/aerothemeplasma}
DEV=${DEV:-$HOME/src}
BK='\.(bak|before|orig|rej)|\.bak-|\.before-|~$'
BIN='\.(png|svgz?|wav|ogg|ico|jpg|gif|bmp|ttf|mo|so|o)$'
: > "$P/BASES.txt"
SEDF=$(mktemp)
cat > "$SEDF" <<'SED'
s/org\.win7\./org.sevenplasma./g
s#/win7/#/sevenplasma/#g
s/win7folderthumbnail/sevenfolderthumbnail/g
s/win7-/seven-/g
s/win7_/seven_/g
s#"/home/[^/"]*/src/seven-common/#"seven-common/#g
s#/home/[^/" ]*/\.local/bin/##g
# WinRAR is not part of SevenPlasma: the menus use the Compressed (zipped) Folders entries instead
s#win7winrar\.h#win7zip.h#g
s/Win7WinRar::/Win7Zip::/g
s#// WinRAR on an archive: .*#// a ZIP file: "Extract All..." (Compressed (zipped) Folders)#
s#// WinRAR: Add to archive.*#// compressing: "Send to -> Compressed (zipped) folder" below#
SED
rename() { sed -i -f "$SEDF" "$1"; }
mk() {
    local d=$1 name=$2
    cd "$d"
    echo "$name $(git remote get-url origin) $(git rev-parse HEAD)" >> "$P/BASES.txt"
    : > "$P/$name.patch"
    local files; files=$(git diff HEAD --name-only | grep -vE "$BK" | grep -viE "$BIN" || true)
    [ -n "$files" ] && git diff HEAD --text -- $files > "$P/$name.patch"
    git ls-files --others --exclude-standard | grep -vE '(^|/)(build[^/]*|\.cache|__pycache__)/' | grep -vE "$BK" | grep -viE "$BIN" |
        while read -r f; do git diff --no-index --text /dev/null "$f" >> "$P/$name.patch" || true; done
    rename "$P/$name.patch"
}
mk "$ATP" aerothemeplasma
for r in aeroshell-kwin-components aeroshell-workspace aeroshell-sddm-kcm libplasma smod uac-polkit-agent; do mk "$ATP/repos/$r" "atp-$r"; done
mk "$DEV/dolphin-aero" dolphin-aero
mk "$DEV/kio-win7" kio
mk "$DEV/libaero-qt" libaero-qt
mk "$DEV/Kvantum" kvantum
# local copies of ATP plasmoids (~/.local/share/plasma/plasmoids) as a patch on the ATP tree
out=$P/atp-local-overrides.patch; : > "$out"
S=$(mktemp -d)
cd "$ATP"
for d in "$HOME"/.local/share/plasma/plasmoids/*/; do
    id=$(basename "$d")
    src=$(grep -rl "\"Id\": *\"$id\"" --include=metadata.json plasma 2>/dev/null | head -1)
    [ -z "$src" ] && continue
    sd=$(dirname "$src")
    rm -rf "$S/a" "$S/b"; mkdir -p "$S/a/$sd" "$S/b/$sd"
    rsync -a --exclude='*.bak*' --exclude='*.before*' --exclude='*.orig' --exclude='*.png' --exclude='*.svgz' --exclude='*.jpg' "$sd/" "$S/a/$sd/"
    rsync -a --exclude='*.bak*' --exclude='*.before*' --exclude='*.orig' --exclude='*.png' --exclude='*.svgz' --exclude='*.jpg' "$d" "$S/b/$sd/"
    (cd "$S" && git diff --no-index --text a b | sed -e 's#^diff --git a/[ab]/#diff --git a/#' -e 's# b/b/# b/#' \
        -e 's#^--- a/a/#--- a/#' -e 's#^+++ b/b/#+++ b/#') >> "$out" || true
done
rename "$out"
# the logon screen
F=plasma/sddm/sddm-theme-mod/Main.qml
rm -rf "$S/a" "$S/b"; mkdir -p "$S/a/$(dirname $F)" "$S/b/$(dirname $F)"
cp "$F" "$S/a/$F"; cp "$DEV/win7-common/boot/sddm-Main.qml" "$S/b/$F"
(cd "$S" && git diff --no-index --text a b | sed -e 's#^diff --git a/[ab]/#diff --git a/#' -e 's# b/b/# b/#' \
    -e 's#^--- a/a/#--- a/#' -e 's#^+++ b/b/#+++ b/#') > "$P/atp-sddm.patch" || true
rename "$P/atp-sddm.patch"
rm -rf "$S"
grep -al "$USER" "$P"/*.patch && { echo "personal paths in the patches above"; exit 1; } || true
wc -l "$P"/*.patch | tail -1
