#!/usr/bin/env bash
# Builds, tests and packages V Media AI AutoFrame for macOS (one universal arm64 + x86_64 build).
#
#   scripts/setup-macos.sh      (once - dependencies)
#   scripts/package-macos.sh    -> release/VMedia-AI-AutoFrame-<ver>-macOS.dmg
#
# The DMG contains:
#   Install V Media AI AutoFrame.pkg   installer -> /Library/Application Support/obs-studio/plugins
#   Manual install/vmedia-ai-autoframe.plugin
#   READ ME FIRST.txt
#
# Code signing: ad-hoc by default (free, no Apple account). Set MACOS_SIGN_IDENTITY
# ("Developer ID Application: ...") and MACOS_INSTALLER_IDENTITY ("Developer ID Installer: ...")
# to sign for real; notarize the DMG afterwards with `xcrun notarytool`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
VERSION="$(sed -nE 's/^[[:space:]]*VERSION ([0-9.]+).*/\1/p' CMakeLists.txt | head -1)"
TP="$ROOT/third_party"
BUILD="$ROOT/build-macos"
OUT="$ROOT/release"
SIGN_ID="${MACOS_SIGN_IDENTITY:--}"
INSTALLER_ID="${MACOS_INSTALLER_IDENTITY:-}"
BUNDLE_ID="com.vmediaproduction.autoframe"

echo "== V Media AI AutoFrame $VERSION for macOS"

# ------------------------------------------------------------------ build (universal)
cmake -S . -B "$BUILD" -G "Unix Makefiles" \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
	-DOBS_FRAMEWORK_DIR="$TP/obs-mac/universal"
cmake --build "$BUILD" --parallel "$(sysctl -n hw.ncpu)"

PLUGIN="$BUILD/vmedia-ai-autoframe.plugin"
BIN="$PLUGIN/Contents/MacOS/vmedia-ai-autoframe"
[ -f "$BIN" ] || { echo "plugin binary missing: $BIN" >&2; exit 1; }
echo "-- plugin architectures: $(lipo -archs "$BIN")"
# Lowest macOS the shipped binaries run on (ONNX Runtime may need newer than the plugin itself)
minos_of() { vtool -show-build "$1" 2>/dev/null | awk '/minos/ {print $2}' | sort -V | tail -1; }
ORT_MINOS="$(minos_of "$PLUGIN/Contents/Resources/runtime/libonnxruntime.dylib")"
MIN_MACOS="$(printf '12.0
%s
' "${ORT_MINOS:-12.0}" | sort -V | tail -1)"
echo "-- minimum macOS: plugin $(minos_of "$BIN"), ONNX Runtime ${ORT_MINOS:-?} -> package requires $MIN_MACOS"
echo "-- linked libraries:"
otool -L "$BIN" | sed 1d

# ------------------------------------------------------------------ tests
echo "== unit tests"
"$BUILD/vmaf-tests"
echo "== detector test (real ONNX Runtime + YOLOX)"
"$BUILD/vmaf-detector-test"
if arch -x86_64 /usr/bin/true 2>/dev/null; then
	echo "== unit tests, Intel slice (Rosetta)"
	arch -x86_64 "$BUILD/vmaf-tests"
fi

# Real-OBS load test: the checker runs from inside OBS.app so the plugin resolves libobs exactly
# like it does in OBS itself.
load_check() { # flavour(apple|intel) [arch prefix]
	local app="$TP/obs-mac/$1/OBS.app"
	cp "$BUILD/macos-load-check" "$app/Contents/MacOS/macos-load-check"
	shift
	"$@" "$app/Contents/MacOS/macos-load-check" "$PLUGIN"
	rm -f "$app/Contents/MacOS/macos-load-check"
}
echo "== load test in OBS (Apple silicon build of OBS)"
if [ "$(uname -m)" = arm64 ]; then
	load_check apple
	if arch -x86_64 /usr/bin/true 2>/dev/null; then
		echo "== load test in OBS (Intel build of OBS, via Rosetta)"
		load_check intel arch -x86_64
	fi
else
	load_check intel
fi

# ------------------------------------------------------------------ sign
# Inner code first (the ONNX Runtime library), then the bundle that seals it.
codesign --force --timestamp=none --sign "$SIGN_ID" "$PLUGIN/Contents/Resources/runtime/libonnxruntime.dylib"
codesign --force --timestamp=none --sign "$SIGN_ID" "$PLUGIN"
codesign --verify --strict --verbose=2 "$PLUGIN"

# ------------------------------------------------------------------ pkg
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/root" "$WORK/resources" "$WORK/dmg/Manual install"
ditto "$PLUGIN" "$WORK/root/vmedia-ai-autoframe.plugin"

# Never let Installer "relocate" the bundle to some other copy it finds on disk
pkgbuild --analyze --root "$WORK/root" "$WORK/component.plist"
plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/component.plist"
pkgbuild --root "$WORK/root" \
	--component-plist "$WORK/component.plist" \
	--identifier "$BUNDLE_ID" \
	--version "$VERSION" \
	--install-location "/Library/Application Support/obs-studio/plugins" \
	"$WORK/component.pkg"

cp LICENSE "$WORK/resources/LICENSE.txt"
cat >"$WORK/resources/welcome.html" <<EOF
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>V Media AI AutoFrame $VERSION</h2>
<p>AI auto-framing filter for OBS Studio by V Media Production. It keeps a preacher, presenter or
performer framed with smooth digital pan, tilt and zoom &mdash; fully offline.</p>
<p><b>Quit OBS Studio before you continue.</b></p>
<p>Needs OBS Studio 30 or newer (Apple silicon or Intel), macOS $MIN_MACOS or newer.</p>
<p>After installing: open OBS &rarr; right-click your camera &rarr; <b>Filters</b> &rarr;
<b>+</b> &rarr; <b>V Media AI AutoFrame</b>.</p>
</body></html>
EOF
cat >"$WORK/resources/conclusion.html" <<'EOF'
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>Installed</h2>
<p>Start OBS Studio, right-click your camera source &rarr; <b>Filters</b> &rarr; <b>+</b> &rarr;
<b>V Media AI AutoFrame</b>.</p>
<p>To uninstall, delete<br><code>/Library/Application Support/obs-studio/plugins/vmedia-ai-autoframe.plugin</code></p>
</body></html>
EOF
cat >"$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>V Media AI AutoFrame $VERSION</title>
    <organization>com.vmediaproduction</organization>
    <welcome file="welcome.html"/>
    <license file="LICENSE.txt"/>
    <conclusion file="conclusion.html"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <volume-check>
        <allowed-os-versions><os-version min="$MIN_MACOS"/></allowed-os-versions>
    </volume-check>
    <choices-outline>
        <line choice="default"><line choice="$BUNDLE_ID"/></line>
    </choices-outline>
    <choice id="default"/>
    <choice id="$BUNDLE_ID" visible="false"><pkg-ref id="$BUNDLE_ID"/></choice>
    <pkg-ref id="$BUNDLE_ID" version="$VERSION" onConclusion="none">component.pkg</pkg-ref>
</installer-gui-script>
EOF
PKG="$WORK/dmg/Install V Media AI AutoFrame.pkg"
SIGN_ARGS=()
[ -n "$INSTALLER_ID" ] && SIGN_ARGS=(--sign "$INSTALLER_ID")
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
	--package-path "$WORK" ${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"} "$PKG"
echo "-- package payload:"
pkgutil --payload-files "$PKG" | grep -v _CodeSignature

# ------------------------------------------------------------------ dmg
ditto "$PLUGIN" "$WORK/dmg/Manual install/vmedia-ai-autoframe.plugin"
cat >"$WORK/dmg/READ ME FIRST.txt" <<EOF
V Media AI AutoFrame $VERSION for macOS - by V Media Production
=====================================================================

Requirements: OBS Studio 30 or newer, macOS $MIN_MACOS or newer, Apple silicon or Intel Mac.

INSTALL (recommended)
  1. Quit OBS Studio.
  2. Double-click "Install V Media AI AutoFrame.pkg" and follow the steps.

     macOS may say the package "can't be opened because Apple cannot check it for malicious
     software" (it is not notarized by Apple). Then:
       - macOS 15 Sequoia and newer: open System Settings > Privacy & Security, scroll down,
         click "Open Anyway" next to the message about the package, and confirm.
       - macOS 12-14: right-click (or Control-click) the .pkg > Open > Open.

  3. Start OBS. Right-click your camera source > Filters > + > V Media AI AutoFrame.

MANUAL INSTALL (no admin password)
  1. Quit OBS.
  2. In Finder press Shift+Cmd+G and go to:
       ~/Library/Application Support/obs-studio/plugins
     (create the "plugins" folder if it does not exist)
  3. Copy "Manual install/vmedia-ai-autoframe.plugin" into that folder.
  4. Open Terminal and run (removes the download quarantine so OBS may load it):
       xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/vmedia-ai-autoframe.plugin
  5. Start OBS.

GPU
  "Use GPU" in the filter settings uses Core ML (Apple GPU / Neural Engine). CPU works everywhere.

UNINSTALL
  Delete vmedia-ai-autoframe.plugin from
    /Library/Application Support/obs-studio/plugins   (installer)
    ~/Library/Application Support/obs-studio/plugins  (manual)

Guide and help: https://github.com/vinitdeep/obsautoframe
EOF

mkdir -p "$OUT"
DMG="$OUT/VMedia-AI-AutoFrame-$VERSION-macOS.dmg"
rm -f "$DMG"
hdiutil create -volname "V Media AI AutoFrame $VERSION" -srcfolder "$WORK/dmg" -ov -format UDZO "$DMG"
[ "$SIGN_ID" != "-" ] && codesign --force --sign "$SIGN_ID" "$DMG"
hdiutil verify "$DMG"
(cd "$OUT" && shasum -a 256 "$(basename "$DMG")" >"$(basename "$DMG").sha256")
echo
echo "Created $DMG"
cat "$DMG.sha256"
