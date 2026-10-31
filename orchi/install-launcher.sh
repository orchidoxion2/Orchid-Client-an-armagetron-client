#!/usr/bin/env bash
# Adds Orchid Client to the application menu/search. Run it again after moving this folder.
# Remove with: rm ~/.local/share/applications/orchid-client.desktop
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"

mkdir -p "$APPS"
# the entry of the earlier name, Orchi Tron
rm -f "$APPS/orchi-tron.desktop"

cat > "$APPS/orchid-client.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Orchid Client
Comment=An Armagetron client
Exec="$DIR/AppRun"
Icon=$DIR/org.armagetronad.armagetronad.png
Terminal=false
Categories=Game;ActionGame;
Keywords=orchid;armagetron;tron;light;cycle;
EOF
chmod +x "$APPS/orchid-client.desktop"
command -v update-desktop-database > /dev/null && update-desktop-database "$APPS" || true

echo "Orchid Client added. Press the Windows key and type: orchid"
