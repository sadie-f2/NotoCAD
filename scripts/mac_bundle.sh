#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Sadie Forbes
#
# Builds, verifies and packages NotoCAD.app.
#
# The steps are ordered so the cheap honest checks come before the expensive
# trusting ones, and the one that matters most is the self-containment test:
# a dev machine has Homebrew's Qt sitting on the library path, so a bundle
# with missing frameworks still launches HERE by accident and fails on the
# first Mac that isn't this one. That test, not "it opened on my machine",
# is what says the bundle can travel.
#
# Usage:
#   scripts/mac_bundle.sh                  ad-hoc signature (local testing)
#   scripts/mac_bundle.sh "Developer ID Application: ..."   real identity
#
# Output: build-bundle/NotoCAD.app and build-bundle/NotoCAD-<version>.dmg
set -euo pipefail

IDENTITY="${1:--}"   # "-" is codesign's ad-hoc identity
REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/build-bundle"

# How far back the bundle claims to run. Everything below enforces it: our
# own code compiles against it, and the gate refuses any bundled binary
# stamped newer. arm64 hardware starts at macOS 11, so 12.0 is nearly the
# whole installed base.
DEPLOY_TARGET="${NCAD_DEPLOY_TARGET:-12.0}"

# Prefer the official Qt binaries (built for macOS 12+) over Homebrew's.
# Homebrew bottles are compiled per-OS-release for THIS machine -- a bundle
# made from them refuses to launch on any older macOS, which is how the
# first field install failed. Homebrew remains fine for development builds;
# it is only distribution that needs the portable Qt.
QT_PREFIX="${NCAD_QT_PREFIX:-}"
if [ -z "$QT_PREFIX" ]; then
  QT_PREFIX="$(ls -d "$HOME"/Qt/6.*/macos 2>/dev/null | sort -V | tail -1 || true)"
fi
if [ -z "$QT_PREFIX" ]; then
  QT_PREFIX="$(brew --prefix qt)"
  echo "WARNING: no official Qt found under ~/Qt -- falling back to Homebrew's."
  echo "         This bundle will only run on macOS $(sw_vers -productVersion | cut -d. -f1).x and newer."
fi
echo "Qt:     $QT_PREFIX"
echo "min OS: $DEPLOY_TARGET"

echo "== configure + build (Release, bundle shape) =="
cmake -S "$REPO" -B "$BUILD" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOY_TARGET" \
      -DNCAD_BUILD_GUI=ON \
      -DNCAD_MACOS_BUNDLE=ON \
      -DNCAD_BUILD_TESTS=ON
cmake --build "$BUILD"

echo "== tests (a bundle of a broken build is a well-packaged broken build) =="
"$BUILD/tests/ncad_tests" > /dev/null

APP="$BUILD/src/gui/NotoCAD.app"
VERSION="$(sed -n 's/^project(NotoCAD VERSION \([0-9.]*\).*/\1/p' "$REPO/CMakeLists.txt")"

echo "== macdeployqt: Qt frameworks into the bundle =="
# A re-run over an already-deployed bundle half-works and half-warns, so
# always deploy into a freshly built app. The build above guarantees that
# only on the first run; force it by clearing the marker macdeployqt leaves.
if [ -d "$APP/Contents/Frameworks" ]; then
  echo "   (stale Frameworks/ found -- rebuilding the .app from scratch)"
  rm -rf "$APP"
  cmake --build "$BUILD"
fi
"$QT_PREFIX/bin/macdeployqt" "$APP" 2>"$BUILD/deploy_err.txt" || true

echo "== prune: plugins NotoCAD does not use =="
# macdeployqt copies every plugin the Qt install offers. The PDF, SVG, WebP
# and virtual-keyboard ones drag in frameworks it then fails to resolve from
# Homebrew's split formulae -- and a wireframe CAD shell renders none of
# those formats. Removing the plugin removes its dependency tree.
rm -rf "$APP/Contents/PlugIns/virtualkeyboard" \
       "$APP/Contents/PlugIns/platforminputcontexts"
rm -f "$APP/Contents/PlugIns/imageformats/libqpdf.dylib" \
      "$APP/Contents/PlugIns/imageformats/libqsvg.dylib" \
      "$APP/Contents/PlugIns/imageformats/libqwebp.dylib"

echo "== fixup: transitive Homebrew deps macdeployqt missed =="
# It walks Qt's own modules reliably, but Homebrew splits what Qt expects to
# be one install across formulae (brotli, freetype, ICU...), and second-order
# deps get left pointing at /opt/homebrew. Copy each one in and repoint the
# reference; iterate, because the copied library may have leaks of its own.
scan_leaks() {
  { find "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" \
      -type f \( -name '*.dylib' -o -perm +111 \) 2>/dev/null
  } | while IFS= read -r bin; do
        otool -L "$bin" 2>/dev/null | awk -v f="$bin" \
          '/^\t(\/opt\/homebrew|\/usr\/local)\// { print f "|" $1 }'
      done
}
for pass in 1 2 3 4 5 6 7 8; do
  found=0
  while IFS='|' read -r file dep; do
    [ -z "$dep" ] && continue
    found=1
    base="$(basename "$dep")"
    if [ ! -f "$APP/Contents/Frameworks/$base" ]; then
      echo "   + $base (for $(basename "$file"))"
      cp "$dep" "$APP/Contents/Frameworks/"
      chmod u+w "$APP/Contents/Frameworks/$base"
    fi
    if [ "$(basename "$file")" = "$base" ]; then
      # otool -L's first indented line is the library's own install-name ID,
      # which -change cannot rewrite -- it needs -id.
      install_name_tool -id "@executable_path/../Frameworks/$base" "$file" 2>/dev/null
    else
      install_name_tool -change "$dep" "@executable_path/../Frameworks/$base" "$file" 2>/dev/null
    fi
  done < <(scan_leaks)
  [ "$found" -eq 0 ] && break
done

echo "== leak check: nothing may still point into Homebrew =="
# Anything still resolved via /opt/homebrew or /usr/local does not exist on
# someone else's Mac. The fixup above should have converged; this is the
# gate that says whether it actually did.
LEAKS="$(scan_leaks)"
if [ -n "$LEAKS" ]; then
  echo "LEAKED HOMEBREW PATHS:"
  echo "$LEAKS" | tr '|' '\t'
  echo "The bundle would only run on machines with these formulae installed."
  exit 1
fi
echo "   clean"

echo "== min-OS check: nothing may demand a newer macOS than $DEPLOY_TARGET =="
# dyld refuses a library whose min-OS is newer than the running system, with
# a "version incompatibility" complaint -- which a recipient reads as the app
# being broken, not as a build-machine artifact. Every Mach-O in the bundle
# must claim DEPLOY_TARGET or older.
TOO_NEW="$(
  find "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/PlugIns" \
      -type f \( -name '*.dylib' -o -perm +111 \) 2>/dev/null \
  | while IFS= read -r bin; do
      minos="$(otool -l "$bin" 2>/dev/null | awk '/^ *minos/ {print $2; exit}')"
      [ -z "$minos" ] && continue
      if [ "$(printf '%s\n%s\n' "$DEPLOY_TARGET" "$minos" | sort -V | tail -1)" != "$DEPLOY_TARGET" ]; then
        echo "$bin needs macOS $minos"
      fi
    done
)"
if [ -n "$TOO_NEW" ]; then
  echo "BINARIES DEMANDING A NEWER macOS:"
  echo "$TOO_NEW"
  echo "The bundle would refuse to launch on macOS $DEPLOY_TARGET."
  exit 1
fi
echo "   all binaries claim macOS $DEPLOY_TARGET or older"

echo "== sign ($IDENTITY) =="
codesign --deep --force --sign "$IDENTITY" "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
if [ "$IDENTITY" != "-" ]; then
  spctl -a -vv --type exec "$APP" || true
else
  echo "   (ad-hoc: Gatekeeper will refuse this on other Macs -- expected"
  echo "    until a Developer ID signs it)"
fi

echo "== self-containment test: launch with Homebrew hidden =="
# env -i strips the search paths that make missing libraries invisible on a
# dev machine. dyld resolves everything at exec, so surviving 3 seconds
# means the linkage is genuinely self-contained; a missing framework dies
# immediately with a dyld error instead. The bundle ships only the cocoa
# platform plugin, so this launches the real window -- expect a 3-second
# flash of NotoCAD while the test runs.
set +e
env -i HOME="$HOME" \
  perl -e 'alarm 3; exec @ARGV or die' "$APP/Contents/MacOS/NotoCAD" 2>"$BUILD/launch_err.txt"
STATUS=$?
set -e
if [ $STATUS -eq 142 ]; then      # SIGALRM: it ran until we stopped it
  echo "   self-contained"
elif grep -qi 'dyld\|Library not loaded' "$BUILD/launch_err.txt"; then
  echo "SELF-CONTAINMENT FAILED:"
  cat "$BUILD/launch_err.txt"
  exit 1
else
  echo "   exited with status $STATUS -- not a dyld failure, but look:"
  cat "$BUILD/launch_err.txt"
  exit 1
fi

echo "== disk image =="
DMG="$BUILD/NotoCAD-$VERSION.dmg"
rm -f "$DMG"
STAGE="$BUILD/dmg-stage"
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "NotoCAD $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG" > /dev/null
rm -rf "$STAGE"

echo
echo "done: $DMG"
echo "      $APP"
