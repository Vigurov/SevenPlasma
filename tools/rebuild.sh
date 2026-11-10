#!/bin/sh
# Re-run the whole asset tracing: index the extracted Windows tree, match the installed Win7-look assets,
# write extract/recipes.json. W = work dir with wimx/5 (extracted install.wim image) and manual_recipes.json.
set -e
W=${W:-$HOME/.cache/win7-work}
T=$(dirname "$0")
cd "$W"
python3 "$T/win7index.py" wimx/5 win7index.json
python3 "$T/win7match.py" wimx/5 win7index.json win7match.json matchset/icons \
    "$HOME/.local/share/win7-aero" "$HOME/.local/share/win7-control" "$HOME/.local/share/win7-gadgets" \
    "$HOME/.local/share/win7-wmp" "$HOME/.local/share/win7-paint" "$HOME/.local/share/win7-photoviewer" \
    "$HOME/.local/share/win7-snip" "$HOME/.local/share/win7-calc" "$HOME/.local/share/fonts/Windows 7" \
    "/usr/share/sounds/Windows 7" "$HOME/.local/share/wallpapers/Windows7"
python3 "$T/win7recipes.py" win7match.json manual_recipes.json "$T/../extract/recipes.json"
