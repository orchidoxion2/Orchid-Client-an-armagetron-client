#!/usr/bin/env bash
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TITLE="Orchid Client"
APPNAME=orchid-client
OUT="$ROOT/build/orchi"
APPDIR="$OUT/appdir"

test -f "$ROOT/configure" || (cd "$ROOT" && ./bootstrap.sh)

if test "$(cat "$OUT/build/title.stamp" 2>/dev/null)" != "$TITLE"; then
    rm -rf "${OUT:?}/build"
fi
mkdir -p "$OUT/build"
cd "$OUT/build"
if ! test -f Makefile; then
    progname=armagetronad progtitle="$TITLE" "$ROOT/configure" --prefix=/usr \
        --disable-uninstall --disable-restoreold --enable-etc --enable-automakedefaults \
        --disable-useradd --disable-sysinstall --disable-initscripts --enable-curl --disable-tests
    echo "$TITLE" > title.stamp
fi
make -j"$(nproc)"

rm -rf "${APPDIR:?}"
make install DESTDIR="$APPDIR"

rm -rf "${APPDIR:?}/usr/share/games/armagetronad/music"

cd "$ROOT/docker/build/context"
APPDIR="$APPDIR" PACKAGE=armagetronad portable/build > "$OUT/portable.log"
cp "$APPDIR"/usr/share/games/armagetronad/desktop/icons/48x48/*.png "$APPDIR/.DirIcon"

SYSTEM_LIBS='^(ld-linux|linux-vdso|libc\.so|libm\.so|libmvec|libresolv|libgcc_s|libGL\.so|libGLX|libGLdispatch|libOpenGL|libEGL|libdrm|libgbm|libX11|libX11-xcb|libxcb|libXau|libXdmcp|libXext|libXfixes|libsystemd)'
for lib in $(LD_LIBRARY_PATH="$APPDIR/usr/lib" ldd "$APPDIR/usr/bin/armagetronad" \
             | grep -v "$APPDIR/usr/lib" | awk '$3 ~ /^\// {print $3}'); do
    name=$(basename "$lib")
    echo "$name" | grep -qE "$SYSTEM_LIBS" || cp -L "$lib" "$APPDIR/usr/lib/$name"
done

cp "$ROOT/orchi/install-launcher.sh" "$APPDIR/"

cd "$OUT"
rm -rf "${APPNAME:?}"
mv "$APPDIR" "$APPNAME"
tar -czf "$APPNAME-linux-x86_64.tar.gz" "$APPNAME"
echo "Package: $OUT/$APPNAME-linux-x86_64.tar.gz"
