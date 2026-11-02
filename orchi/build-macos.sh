#!/usr/bin/env bash
# Builds Orchid Client for macOS on a Mac with Homebrew:
#   build/mac/Orchid Client.app
#   build/mac/orchid-client-macos-<arm64|x86_64>.zip   (the app, zipped for downloading)
# The app runs on Macs with the same processor type as the Mac it was built on.
# Needs: Xcode command line tools, and from Homebrew:
#   autoconf automake libtool pkg-config boost sdl2 sdl2_image sdl2_mixer libxml2 freetype ftgl libpng glew dylibbundler
# GitHub Actions runs this in .github/workflows/macos.yml.
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TITLE="Orchid Client"
MAC="$ROOT/build/mac"
ARCH="$(uname -m)"
JOBS="$(sysctl -n hw.ncpu)"
BREW="$(brew --prefix)"
APP="$MAC/$TITLE.app"
ZIP="$MAC/orchid-client-macos-$ARCH.zip"

mkdir -p "$MAC"

# protobuf 3.21: the newer versions from Homebrew need abseil, which this game's build does not set up
PROTOBUF="$MAC/protobuf"
if ! test -x "$PROTOBUF/bin/protoc"; then
    mkdir -p "$MAC/protobuf-src" && cd "$MAC/protobuf-src"
    curl -fsSL -o protobuf.tar.gz \
        https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz
    tar xzf protobuf.tar.gz
    cd protobuf-3.21.12
    ./configure --prefix="$PROTOBUF" --disable-shared CXXFLAGS="-O2 -std=c++14" > configure.log
    make -j"$JOBS" > make.log
    make install > install.log
fi

# libcurl comes with macOS; describe it to pkg-config, so the game uses it instead of bundling another one
mkdir -p "$MAC/pkgconfig"
cat > "$MAC/pkgconfig/libcurl.pc" <<EOF
Name: libcurl
Description: libcurl from macOS
Version: $(curl-config --version | awk '{print $2}')
Libs: -lcurl
Cflags:
EOF

export PATH="$PROTOBUF/bin:$PATH"
export PKG_CONFIG_PATH="$MAC/pkgconfig:$PROTOBUF/lib/pkgconfig:$BREW/opt/libxml2/lib/pkgconfig:$PKG_CONFIG_PATH"

test -f "$ROOT/configure" || (cd "$ROOT" && ./bootstrap.sh)

# laid out like an app bundle: the program in Contents/MacOS, the game files in Contents/Resources
BUILD="$MAC/build"
mkdir -p "$BUILD" && cd "$BUILD"
if ! test -f Makefile; then
    progname=armagetronad progtitle="$TITLE" "$ROOT/configure" \
        --disable-restoreold --enable-automakedefaults --disable-useradd --disable-sysinstall \
        --disable-initscripts --disable-uninstall --disable-etc --disable-games --disable-desktop \
        --disable-tests --enable-curl \
        --prefix=/Contents --bindir=/Contents/MacOS --datadir=/Contents/Resources --sysconfdir=/Contents/Resources/etc \
        --libdir=/Contents/Frameworks \
        --with-boost="$BREW" \
        CPPFLAGS="-I$BREW/include" LDFLAGS="-L$BREW/lib -headerpad_max_install_names" \
        > configure.log
fi
make -j"$JOBS" > make.log 2>&1 || { tail -50 make.log; exit 1; }

rm -rf "$APP" "$MAC/install"
make install DESTDIR="$MAC/install" > install.log
mkdir -p "$APP"
mv "$MAC/install/Contents" "$APP/Contents"
rm -rf "$MAC/install" "$APP/Contents/Resources/armagetronad/music"

# the game finds its files relative to the program, in Contents/Resources/armagetronad
EXE=armagetronad
test -x "$APP/Contents/MacOS/$EXE" || { echo "no $EXE in the app:"; ls -R "$APP/Contents" | head -40; exit 1; }

VERSION="$(cd "$ROOT" && git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')"
test -n "$VERSION" || VERSION=1.0
cp "$ROOT/orchi/art/orchid.icns" "$APP/Contents/Resources/$TITLE.icns"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>en</string>
    <key>CFBundleExecutable</key><string>$EXE</string>
    <key>CFBundleIconFile</key><string>$TITLE.icns</string>
    <key>CFBundleIdentifier</key><string>io.github.orchidoxion2.orchidclient</string>
    <key>CFBundleName</key><string>$TITLE</string>
    <key>CFBundleDisplayName</key><string>$TITLE</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
    <key>LSMinimumSystemVersion</key><string>$(sw_vers -productVersion | cut -d. -f1).0</string>
</dict>
</plist>
EOF

# copy the Homebrew libraries into the app and point the program at them
mkdir -p "$APP/Contents/Frameworks"
dylibbundler -cd -of -b -x "$APP/Contents/MacOS/$EXE" -d "$APP/Contents/Frameworks/" \
    -p @executable_path/../Frameworks/ > "$MAC/dylibbundler.log"

# changing the libraries breaks their signatures; Apple Silicon Macs only start signed programs,
# so sign everything again without a developer certificate ("ad hoc")
codesign --force --deep --sign - "$APP"
codesign --verify --deep "$APP"

rm -f "$ZIP"
(cd "$MAC" && ditto -c -k --keepParent "$TITLE.app" "$ZIP")
echo "App: $APP"
echo "Package: $ZIP"
