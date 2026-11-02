#!/usr/bin/env bash
# Cross-compiles Orchid Client for Windows with MinGW:
#   build/win/orchid-client-windows.zip
# Needs: i686-w64-mingw32-g++-posix, python3 with Pillow, zip, git
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TITLE="Orchid Client"
WIN="$ROOT/build/win"
LIBS="$WIN/winlibs"
GEN="$WIN/gen"
OBJ="$WIN/obj"
DIST="$WIN/Orchid Client"
CXX=i686-w64-mingw32-g++-posix
CC=i686-w64-mingw32-gcc-posix
JOBS=$(nproc)

mkdir -p "$WIN" "$GEN" "$OBJ"

if ! test -d "$LIBS"; then
    git clone -q https://gitlab.com/armagetronad/winlibs.git "$LIBS"
    git -C "$LIBS" checkout -q 1ed9319621779ddb76837ce645e1a68ba7f131bd
fi

if ! test -x "$WIN/protoc-host/src/protoc"; then
    mkdir -p "$WIN/protoc-host" && cd "$WIN/protoc-host"
    "$LIBS/protobuf/configure" --disable-shared CXXFLAGS="-O2 -std=gnu++11 -w" > configure.log
    make -j"$JOBS" -C src protoc > make.log
fi
PROTOC="$WIN/protoc-host/src/protoc"

if ! test -f "$WIN/protobuf-win/src/.libs/libprotobuf.a"; then
    mkdir -p "$WIN/protobuf-win" && cd "$WIN/protobuf-win"
    "$LIBS/protobuf/configure" --host=i686-w64-mingw32 --disable-shared --with-protoc="$PROTOC" \
        CXX="$CXX" CC="$CC" CXXFLAGS="-O2 -std=gnu++11 -w" > configure.log
    make -j"$JOBS" -C src libprotobuf.la > make.log
fi

if ! test -f "$WIN/ftgl/libftgl.a"; then
    mkdir -p "$WIN/ftgl" && cd "$WIN/ftgl"
    for s in $(grep -oE 'Unit filename="[^"]+\.cpp"' "$LIBS/win32/code_blocks/FTGL.cbp" | sed 's,Unit filename="\.\./\.\./,,;s,"$,,'); do
        echo "$CXX -c -O2 -w -DWIN32 -D_WINDOWS -DFTGL_LIBRARY_STATIC -DNDEBUG -I$LIBS/FTGL/win32/freetype/include -I$LIBS/FTGL/win32/freetype/include/freetype2 -I$LIBS/FTGL/src -I$LIBS/FTGL $LIBS/$s -o $(echo "$s" | tr / _).o"
    done | xargs -P"$JOBS" -I{} sh -c {}
    i686-w64-mingw32-ar rcs libftgl.a ./*.o
fi

mkdir -p "$GEN/protobuf"
for p in "$ROOT"/src/protobuf/*.proto; do
    test "$GEN/protobuf/$(basename "${p%.proto}").pb.cc" -nt "$p" || \
        "$PROTOC" --proto_path="$ROOT/src/protobuf" --cpp_out="$GEN/protobuf" "$p"
done

cat > "$GEN/aa_config.h.new" <<EOF
#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED
#include "config_common.h"
#undef HAVE_BOOST_THREAD
#define HAVE_PTHREAD 1
#define HAVE_LIBSDL_MIXER 1
#define HAVE_FTGL_H
#define PROGTITLE "$TITLE"
#ifndef DEBUG
#define USER_DATA_DIR  "\${APPDATA}/$TITLE"
#define SCREENSHOT_DIR "\${MYPICTURES}/$TITLE"
#else
#define USER_DATA_DIR  "."
#endif
#endif
EOF
if ! cmp -s "$GEN/aa_config.h.new" "$GEN/aa_config.h"; then
    mv "$GEN/aa_config.h.new" "$GEN/aa_config.h"
    rm -f "$OBJ"/*.o
fi

python3 - "$ROOT" "$GEN" <<'PYEOF'
import sys
from PIL import Image
root, gen = sys.argv[1], sys.argv[2]
icon = Image.open(f"{root}/desktop/icons/128x128/armagetronad.png").convert("RGBA")
icon.save(f"{gen}/orchid.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128)])
PYEOF
cp "$ROOT/src/win32/app.manifest" "$GEN/"
cat > "$GEN/orchid.rc" <<EOF
#include <winuser.h>
1 ICON "orchid.ico"
CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST "app.manifest"
EOF
i686-w64-mingw32-windres -I"$GEN" "$GEN/orchid.rc" -O coff -o "$OBJ/orchid.res.o"

INCLUDES="-I$GEN -I$GEN/protobuf"
for d in src src/engine src/engine/sound src/network src/render src/tools src/resource src/tools/values \
         src/ui src/win32 src/tron src/tron/cockpit src/tron/zone src/thirdparty/mathexpr \
         src/thirdparty/particles src/thirdparty/particles/ParticleDLL src/thirdparty/shttpd \
         src/thirdparty/scrap src/thirdparty/utf8; do
    INCLUDES="$INCLUDES -I$ROOT/$d"
done
for d in include libxml2/include iconv/include FTGL/src/FTGL FTGL/src FTGL/win32/freetype/include \
         FTGL/win32/freetype/include/freetype2 boost/includes protobuf/src \
         SDL2/i686-w64-mingw32/include/SDL2 SDL2_image/i686-w64-mingw32/include/SDL2 \
         SDL2_mixer/i686-w64-mingw32/include/SDL2; do
    INCLUDES="$INCLUDES -I$LIBS/$d"
done
DEFINES="-DWIN32 -D_WINDOWS -D_MBCS -DNO_SOCKLEN_T -DDONTUSEMEMMANAGER -DNDEBUG -DFTGL_LIBRARY_STATIC"
CXXFLAGS="-O2 -std=gnu++14 -fexceptions -w $DEFINES $INCLUDES"

python3 - "$ROOT/win32/code_blocks/ArmagetronAd.cbp" > "$WIN/sources.txt" <<'PYEOF'
import re, sys
project = open(sys.argv[1]).read()
for name, _, body in re.findall(r'<Unit filename="([^"]+)"\s*(/>|>(.*?)</Unit>)', project, re.S):
    if 'compile="0"' not in (body or "") and re.search(r"\.(cpp|cc|c)$", name):
        print(name.replace("../../", "", 1))
PYEOF
echo "src/tron/gSpotify.cpp" >> "$WIN/sources.txt"
echo "src/tron/cockpit/cSpotify.cpp" >> "$WIN/sources.txt"
echo "src/tron/cockpit/cLeaderboard.cpp" >> "$WIN/sources.txt"
echo "src/tron/gCockpitEditor.cpp" >> "$WIN/sources.txt"
echo "src/tron/gCinematicLyrics.cpp" >> "$WIN/sources.txt"

OBJECTS="$OBJ/orchid.res.o"
: > "$WIN/compile.txt"
for s in $(cat "$WIN/sources.txt"); do
    src="$ROOT/$s"
    case "$s" in src/protobuf/*.pb.cc) src="$GEN/protobuf/$(basename "$s")";; esac
    o="$OBJ/$(echo "$s" | tr / _).o"
    OBJECTS="$OBJECTS $o"
    case "$s" in *.c) compiler="$CC -O2 -w $DEFINES $INCLUDES";; *) compiler="$CXX $CXXFLAGS";; esac
    # recompile when the source or any header it includes changed; the compiler lists
    # the headers in $o.d (-MMD). Without this, a changed header leaves objects built
    # against the old class layouts, which crashes the game.
    stale=
    if ! test -f "$o" || ! test -f "$o.d" || ! test "$o" -nt "$src"; then
        stale=1
    else
        for dep in $(sed -e 's/^[^:]*://' -e 's/\\$//' "$o.d"); do
            case "$dep" in *:) continue;; esac
            if ! test -e "$dep" || test "$dep" -nt "$o"; then stale=1; break; fi
        done
    fi
    test -z "$stale" || echo "$compiler -MMD -MF $o.d -c $src -o $o" >> "$WIN/compile.txt"
done
echo "compiling $(wc -l < "$WIN/compile.txt") files"
xargs -P"$JOBS" -I{} sh -c {} < "$WIN/compile.txt"

$CXX -mwindows ${STRIP--s} -static -o "$WIN/OrchidClient.exe" $OBJECTS \
    "$WIN/ftgl/libftgl.a" "$LIBS/FTGL/win32/freetype/objs/libfreetype248.a" \
    "$WIN/protobuf-win/src/.libs/libprotobuf.a" \
    "$LIBS/libxml2/lib/libxml2.lib" -lmingw32 \
    "$LIBS/SDL2/i686-w64-mingw32/lib/libSDL2main.a" "$LIBS/SDL2/i686-w64-mingw32/lib/libSDL2.dll.a" \
    "$LIBS/SDL2_image/i686-w64-mingw32/lib/libSDL2_image.dll.a" \
    "$LIBS/SDL2_mixer/i686-w64-mingw32/lib/libSDL2_mixer.dll.a" \
    "$LIBS/SDL2_image/i686-w64-mingw32/bin/libpng16-16.dll" \
    -lshell32 -lws2_32 -lwinhttp -lglu32 -lopengl32 -lwinmm

rm -rf "${DIST:?}"
mkdir -p "$DIST"
cp "$WIN/OrchidClient.exe" "$DIST/Orchid Client.exe"
cp "$LIBS"/SDL2/i686-w64-mingw32/bin/*.dll "$LIBS"/SDL2_image/i686-w64-mingw32/bin/*.dll \
   "$LIBS"/SDL2_mixer/i686-w64-mingw32/bin/*.dll "$LIBS/libxml2/lib/libxml2.dll" "$LIBS/iconv/lib/iconv.dll" "$DIST/"
INSTALLED="$ROOT/build/orchi/orchid-client"
"$ROOT/orchi/package.sh" > "$WIN/package.log"
for d in textures sound models resource language; do
    cp -r "$INSTALLED/usr/share/games/armagetronad/$d" "$DIST/$d"
done
cp -r "$INSTALLED/usr/etc/games/armagetronad" "$DIST/config"
find "$DIST" \( -name '*.in' -o -name 'AGENTS.md' \) -delete
cp "$ROOT/COPYING" "$DIST/COPYING.txt"

SYSTEM_DLLS='^(advapi32|gdi32|glu32|imm32|kernel32|oleaut32|opengl32|shell32|user32|version|winhttp|winmm|ws2_32|wsock32|msvcrt|ole32)\.dll$'
missing=$(cd "$DIST" && for f in *.exe *.dll; do i686-w64-mingw32-objdump -p "$f" | awk '/DLL Name/{print $3}'; done | sort -u | while read -r dll; do
    ls | grep -qix "$dll" || echo "$dll" | grep -qiE "$SYSTEM_DLLS" || echo "$dll"
done)
if test -n "$missing"; then
    echo "missing DLLs: $missing" >&2
    exit 1
fi

cd "$WIN"
rm -f orchid-client-windows.zip
zip -qr orchid-client-windows.zip "Orchid Client"
echo "Package: $WIN/orchid-client-windows.zip"
