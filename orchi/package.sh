#!/usr/bin/env bash
# Builds the Orchi Tron client as a portable Linux package:
#   build/orchi/orchi-tron-linux-x86_64.tar.gz
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TITLE="Orchi Tron"
APPNAME=orchi-tron
OUT="$ROOT/build/orchi"
APPDIR="$OUT/appdir"

test -f "$ROOT/configure" || (cd "$ROOT" && ./bootstrap.sh)

# same configure arguments as the upstream release builds (docker/build/Makefile.am)
mkdir -p "$OUT/build"
cd "$OUT/build"
if ! test -f Makefile; then
    progname=armagetronad progtitle="$TITLE" "$ROOT/configure" --prefix=/usr \
        --disable-uninstall --disable-restoreold --enable-etc --enable-automakedefaults \
        --disable-useradd --disable-sysinstall --disable-initscripts --enable-curl --disable-tests
fi
make -j"$(nproc)"

rm -rf "${APPDIR:?}"
make install DESTDIR="$APPDIR"

# Orchi Tron plays no music, don't ship it
rm -rf "${APPDIR:?}/usr/share/games/armagetronad/music"

# upstream portable packaging: AppRun launcher, desktop file, icon, known libraries
cd "$ROOT/docker/build/context"
APPDIR="$APPDIR" PACKAGE=armagetronad portable/build > "$OUT/portable.log"
cp "$APPDIR"/usr/share/games/armagetronad/desktop/icons/48x48/*.png "$APPDIR/.DirIcon"

# also bundle everything else the binary links against, except the libraries
# every desktop provides (glibc, GL/X11 driver stack). AppRun prefers system
# libraries, so the bundled copies only fill gaps (e.g. on Fedora based systems).
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
