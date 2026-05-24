#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION="${1:-1.0.0}"
PKG="sysmon-widget"
BUILD_DIR="$ROOT_DIR/build-deb/${PKG}_${VERSION}_all"
DIST_DIR="$ROOT_DIR/dist"

rm -rf "$BUILD_DIR"
mkdir -p \
  "$BUILD_DIR/DEBIAN" \
  "$BUILD_DIR/opt/sysmon-widget/panels" \
  "$BUILD_DIR/opt/sysmon-widget/utils" \
  "$BUILD_DIR/usr/bin" \
  "$BUILD_DIR/etc/xdg/autostart" \
  "$DIST_DIR"

cp "$ROOT_DIR"/sysmon_widget.py "$ROOT_DIR"/config.py "$ROOT_DIR"/widget.py "$ROOT_DIR"/README.md "$ROOT_DIR"/requirements.txt "$BUILD_DIR/opt/sysmon-widget/"
cp "$ROOT_DIR"/panels/*.py "$BUILD_DIR/opt/sysmon-widget/panels/"
cp "$ROOT_DIR"/utils/*.py "$BUILD_DIR/opt/sysmon-widget/utils/"
cp "$ROOT_DIR"/packaging/linux/sysmon-widget.desktop "$BUILD_DIR/etc/xdg/autostart/sysmon-widget.desktop"

cat > "$BUILD_DIR/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Section: x11
Priority: optional
Architecture: all
Depends: python3, python3-tk, python3-psutil, python3-requests, python3-pil, python3-xlib
Maintainer: live <live@localhost>
Description: Desktop system monitor widget
 A Python and Tkinter desktop widget showing clock, weather,
 network, system stats, storage, and music metadata.
EOF

cat > "$BUILD_DIR/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
chmod 0755 /usr/bin/sysmon-widget
exit 0
EOF

cat > "$BUILD_DIR/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "purge" ]; then
    rm -f /home/live/.config/autostart/sysmon-widget.desktop
fi
exit 0
EOF

cat > "$BUILD_DIR/usr/bin/sysmon-widget" <<'EOF'
#!/usr/bin/env sh
cd /opt/sysmon-widget || exit 1
exec /usr/bin/python3 /opt/sysmon-widget/sysmon_widget.py "$@"
EOF

chmod 0755 "$BUILD_DIR/DEBIAN/postinst" "$BUILD_DIR/DEBIAN/postrm" "$BUILD_DIR/usr/bin/sysmon-widget"
dpkg-deb --root-owner-group --build "$BUILD_DIR" "$DIST_DIR/${PKG}_${VERSION}_all.deb"
