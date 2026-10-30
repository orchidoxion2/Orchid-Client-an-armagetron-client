#!/usr/bin/env bash
# Adds Orchi Tron to the application menu/search. Run it again after moving this folder.
# Remove with: rm ~/.local/share/applications/orchi-tron.desktop
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"

mkdir -p "$APPS"
cat > "$APPS/orchi-tron.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Orchi Tron
Comment=3D light cycle game
Exec="$DIR/AppRun"
Icon=$DIR/org.armagetronad.armagetronad.png
Terminal=false
Categories=Game;ActionGame;
Keywords=orchi;tron;armagetron;light;cycle;
EOF
chmod +x "$APPS/orchi-tron.desktop"
command -v update-desktop-database > /dev/null && update-desktop-database "$APPS" || true

echo "Orchi Tron added. Press the Windows key and type: orchi tron"
