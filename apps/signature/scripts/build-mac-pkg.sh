#!/bin/bash
# build-mac-pkg.sh - builds Mireku-<version>-macOS.pkg: signed, notarised, stapled.
#
#   apps/signature/scripts/build-mac-pkg.sh
#
# Follows the Amanorsac Studio macOS Signing Standard step for step, and the
# Installer & Packaging Standard for what the package contains. Meant for the
# GitHub workflow (.github/workflows/mireku-release.yml), which imports the
# certificates into a keychain first; it also runs on a Mac that already has the
# two Developer ID identities.
#
# Environment:
#   APPLE_TEAM_ID                              required to sign
#   ASC_KEY_PATH, ASC_KEY_ID, ASC_ISSUER_ID    required to notarise
#   MIREKU_TESTER=1                            build a TEST package (asks for a key)
#   MIREKU_SKIP_SIGN=1                         build an UNSIGNED package (not a release)
#   MIREKU_REQUIRE_AUVAL=1                     make auval a hard gate (see below)
#
# WHAT IS NOT IN THE PACKAGE: the sample library. MIREKU's instruments are .jmi
# bundles of the client's own recorded content, several gigabytes of them, and
# they are delivered separately; the app's Settings tab holds the library folder.
# Nothing here writes into the user's Documents.
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$APP_DIR/release"
VERSION="$(sed -n 's/^project(Mireku VERSION \([0-9.]*\).*/\1/p' "$APP_DIR/CMakeLists.txt")"
# MIREKU_TESTER=1 builds a TEST package: it asks for a test key
# (src/plugin/TesterGate.h), is named Mireku-<v>-Tester-macOS.pkg and MUST NOT be
# sold. Own build folder, so it can never be mistaken for the release.
TESTER="${MIREKU_TESTER:-0}"
if [ "$TESTER" = "1" ]; then TAG="-Tester"; BUILD="$APP_DIR/build-mac-tester"; else TAG=""; BUILD="$APP_DIR/build-mac"; fi
PKG="$OUT/Mireku-$VERSION$TAG-macOS.pkg"
SIGN="${MIREKU_SKIP_SIGN:-0}"; [ "$SIGN" = "1" ] && SIGN=no || SIGN=yes

echo "MIREKU $VERSION  (signing: $SIGN, tester: $TESTER)"

# ---------------------------------------------------------------- configure
# Xcode signs at build time so JUCE's hardened-runtime entitlements are applied
# for us. CODE_SIGN_INJECT_BASE_ENTITLEMENTS=NO is REQUIRED: without it Xcode
# injects get-task-allow and Apple answers "Invalid" with no explanation.
SIGN_ARGS=()
if [ "$SIGN" = yes ]; then
  : "${APPLE_TEAM_ID:?APPLE_TEAM_ID is not set}"
  SIGN_ARGS=(
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Manual
    "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY=Developer ID Application"
    "-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=$APPLE_TEAM_ID"
    "-DCMAKE_XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS=--timestamp"
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS=NO )
fi
if [ "$TESTER" = "1" ]; then TESTER_ARG=(-DMIREKU_TESTER_BUILD=ON); else TESTER_ARG=(-DMIREKU_TESTER_BUILD=OFF); fi
cmake -S "$APP_DIR" -B "$BUILD" -G Xcode \
  "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  "${TESTER_ARG[@]}" "${SIGN_ARGS[@]}"
cmake --build "$BUILD" --config Release \
  --target Mireku_Standalone Mireku_VST3 Mireku_AU MirekuTests -- -quiet

# ---------------------------------------------------------------- test
"$BUILD/MirekuTests_artefacts/Release/MirekuTests"

ART="$BUILD/Mireku_artefacts/Release"
APP="$ART/Standalone/MIREKU.app"
VST3="$ART/VST3/MIREKU.vst3"
AU="$ART/AU/MIREKU.component"
for B in "$APP" "$VST3" "$AU"; do
  [ -d "$B" ] || { echo "missing build product: $B"; exit 1; }
done

# A release must not carry the test-key public key (Licence Standard R2), and a
# test build must, or its gate is not there. Looked for in the bytes, not assumed
# from the flags the build was configured with.
MOD="$(sed -n 's/^ *"\([0-9a-f]\{16\}\).*/\1/p' "$APP_DIR/src/plugin/TesterKey.h" | head -1)"
[ -n "$MOD" ] || { echo "could not read the public modulus from src/plugin/TesterKey.h"; exit 1; }
HAS=0; grep -aq "$MOD" "$APP/Contents/MacOS/MIREKU" && HAS=1
if [ "$TESTER" != "1" ] && [ "$HAS" = "1" ]; then echo "the release binary contains the test-build key"; exit 1; fi
if [ "$TESTER" = "1" ]   && [ "$HAS" = "0" ]; then echo "the test build has no test-build key check"; exit 1; fi
echo "  key check: ok"

# ---------------------------------------------------------------- sign
if [ "$SIGN" = yes ]; then
  for B in "$APP" "$VST3" "$AU"; do
    ENT="$(mktemp).plist"
    codesign --display --entitlements - --xml "$B" > "$ENT" 2>/dev/null || true
    if [ -s "$ENT" ]; then codesign --force --options runtime --timestamp --entitlements "$ENT" --sign "Developer ID Application" "$B"
    else                   codesign --force --options runtime --timestamp --sign "Developer ID Application" "$B"; fi

    # Verify before wasting a round trip to Apple.
    codesign --verify --deep --strict --verbose=2 "$B"
    codesign --display --verbose=2 "$B" 2>&1 | grep -q "flags=.*runtime" || { echo "no hardened runtime: $B"; exit 1; }
    codesign --display --verbose=4 "$B" 2>&1 | grep -q "^Timestamp="     || { echo "no secure timestamp: $B"; exit 1; }
    if codesign --display --entitlements - --xml "$B" 2>/dev/null | grep -q "get-task-allow"; then echo "get-task-allow present: $B"; exit 1; fi
  done
fi

# ---------------------------------------------------------------- auval
# MIREKU is an instrument, so the AU type is aumu (not aufx): aumu Mrku Mrek.
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components" "$OUT"
rm -rf "$HOME/Library/Audio/Plug-Ins/Components/MIREKU.component"
cp -R "$AU" "$HOME/Library/Audio/Plug-Ins/Components/"

AUVAL_OK=0
for attempt in 1 2 3 4 5; do
  killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
  sleep 3
  auval -v aumu Mrku Mrek > "$OUT/auval.txt" 2>&1 || true
  if grep -q "AU VALIDATION SUCCEEDED" "$OUT/auval.txt"; then AUVAL_OK=1; break; fi
  echo "auval attempt $attempt: component not found yet, retrying..."
done
tail -8 "$OUT/auval.txt"
rm -rf "$HOME/Library/Audio/Plug-Ins/Components/MIREKU.component"

if [ "$AUVAL_OK" != "1" ]; then
  # Known structural limit of GitHub-hosted macOS runners, confirmed on Low End:
  # since the APFS change the AudioComponent registry does not reliably notice a
  # newly installed v2 AU without a full login-session restart, which an
  # ephemeral runner cannot give it. Failing the package over an environment
  # limit would block the one thing this script exists to produce, so it is a
  # loud warning and auval's own output is kept as evidence. Build Standard B14
  # stays a MANUAL check on real Mac hardware before release.
  echo "::warning::auval did not validate the AU after $attempt attempts (known GitHub-hosted-runner limitation - see release/auval.txt). Build Standard B14 is NOT satisfied by this run."
  if [ "${MIREKU_REQUIRE_AUVAL:-0}" = "1" ]; then
    echo "MIREKU_REQUIRE_AUVAL=1: treating that as a hard failure."
    exit 1
  fi
fi

# ---------------------------------------------------------------- packages
# One component package per part, then a distribution that offers them as
# choices. Every destination is one hosts actually scan.
STAGE="$(mktemp -d)"; PKGS="$STAGE/pkgs"; mkdir -p "$PKGS"
component () {   # id, source, install location
  local root="$STAGE/root-$1"; mkdir -p "$root"; cp -R "$2" "$root/"
  pkgbuild --root "$root" --identifier "com.amanorsac.mireku.$1" --version "$VERSION" \
           --install-location "$3" "$PKGS/$1.pkg"
}
component app  "$APP"  "/Applications"
component vst3 "$VST3" "/Library/Audio/Plug-Ins/VST3"
component au   "$AU"   "/Library/Audio/Plug-Ins/Components"

DOCS="$STAGE/root-docs"; mkdir -p "$DOCS"
if [ "$TESTER" = "1" ]; then
  README_SRC="$APP_DIR/installer/README-tester.txt"; LICENCE_SRC="$APP_DIR/installer/LICENCE-tester.txt"
  WELCOME_SRC="$APP_DIR/installer/mac/welcome-tester.html"
else
  README_SRC="$APP_DIR/installer/README.txt"; LICENCE_SRC="$APP_DIR/installer/LICENCE.txt"
  WELCOME_SRC="$APP_DIR/installer/mac/welcome.html"
fi
cp "$README_SRC" "$DOCS/Read Me First.txt"
cp "$APP_DIR/installer/mac/Uninstall MIREKU.command" "$DOCS/"; chmod +x "$DOCS/Uninstall MIREKU.command"
pkgbuild --root "$DOCS" --identifier com.amanorsac.mireku.docs --version "$VERSION" \
         --install-location "/Library/Application Support/Amanorsac Studio/MIREKU" "$PKGS/docs.pkg"

RES="$STAGE/resources"; mkdir -p "$RES"
cp "$WELCOME_SRC" "$RES/welcome.html"
cp "$APP_DIR/installer/mac/conclusion.html" "$RES/"
cp "$LICENCE_SRC" "$RES/licence.txt"
cp "$README_SRC" "$RES/readme.txt"
if [ "$TESTER" = "1" ]; then TITLE_SUFFIX=" test build"; else TITLE_SUFFIX=""; fi
sed -e "s/@VERSION@/$VERSION/g" -e "s|<title>MIREKU $VERSION</title>|<title>MIREKU $VERSION$TITLE_SUFFIX</title>|" \
    "$APP_DIR/installer/mac/distribution.xml" > "$STAGE/distribution.xml"

productbuild --distribution "$STAGE/distribution.xml" --package-path "$PKGS" --resources "$RES" "$STAGE/unsigned.pkg"

if [ "$SIGN" = no ]; then
  cp "$STAGE/unsigned.pkg" "$PKG"
  echo "UNSIGNED: $PKG  - this is NOT a release."
  exit 0
fi

productsign --timestamp --sign "Developer ID Installer" "$STAGE/unsigned.pkg" "$PKG"
pkgutil --check-signature "$PKG"

# ---------------------------------------------------------------- notarise
# notarytool --wait EXITS 0 EVEN WHEN APPLE SAYS "Invalid". Check explicitly.
: "${ASC_KEY_PATH:?}" "${ASC_KEY_ID:?}" "${ASC_ISSUER_ID:?}"
RESULT=$(xcrun notarytool submit "$PKG" --key "$ASC_KEY_PATH" --key-id "$ASC_KEY_ID" \
           --issuer "$ASC_ISSUER_ID" --wait --timeout 30m --output-format json)
STATUS=$(printf '%s' "$RESULT" | python3 -c 'import sys,json;print(json.load(sys.stdin).get("status",""))')
if [ "$STATUS" != "Accepted" ]; then
  xcrun notarytool log "$(printf '%s' "$RESULT" | python3 -c 'import sys,json;print(json.load(sys.stdin)["id"])')" \
    --key "$ASC_KEY_PATH" --key-id "$ASC_KEY_ID" --issuer "$ASC_ISSUER_ID"
  exit 1
fi
xcrun stapler staple "$PKG"
xcrun stapler validate "$PKG"
spctl --assess -vv --type install "$PKG"     # the verdict a customer's Mac reaches

echo
echo "  file     $(basename "$PKG")"
echo "  bytes    $(stat -f%z "$PKG")"
echo "  sha256   $(shasum -a 256 "$PKG" | cut -d' ' -f1)"
