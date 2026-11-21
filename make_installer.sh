#!/bin/bash
# Build SpaceOpera and package it as a macOS .pkg installer (AU + VST3).
#
#   ./make_installer.sh                    -> unsigned .pkg (for your own Macs)
#
#   Signed + notarized (to give to other people), set these first:
#   export DEV_APP_ID="Developer ID Application: Your Name (TEAMID)"
#   export DEV_INSTALLER_ID="Developer ID Installer: Your Name (TEAMID)"
#   export NOTARY_PROFILE="riot-notary"    # created once with: xcrun notarytool store-credentials
#   ./make_installer.sh

set -euo pipefail
cd "$(dirname "$0")"

NAME="SpaceOpera"
VERSION="1.1.0"
BUNDLE_ID="com.riotaudio.spaceopera"
DEV_APP_ID="${DEV_APP_ID:-}"
DEV_INSTALLER_ID="${DEV_INSTALLER_ID:-}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"

BUILD=build-release
OUT=dist
WORK="$BUILD/pkgwork"
PKG_OUT="$OUT/SpaceOpera-$VERSION-mac.pkg"

echo "==> Building ($NAME $VERSION)"
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DRIOT_COPY_AFTER_BUILD=OFF
cmake --build "$BUILD" --config Release -j8

ART="$BUILD/SpaceOpera_artefacts/Release"
AU="$ART/AU/$NAME.component"
VST3="$ART/VST3/$NAME.vst3"
[ -d "$AU" ]   || { echo "AU not found: $AU"; exit 1; }
[ -d "$VST3" ] || { echo "VST3 not found: $VST3"; exit 1; }

rm -rf "$WORK" && mkdir -p "$WORK"/{root-au,root-vst3,scripts-au,pkgs} "$OUT"
cp -R "$AU"   "$WORK/root-au/"
cp -R "$VST3" "$WORK/root-vst3/"

echo "==> Code signing"
if [ -n "$DEV_APP_ID" ]; then
    for b in "$WORK/root-au/$NAME.component" "$WORK/root-vst3/$NAME.vst3"; do
        codesign --force --deep --options runtime --timestamp --sign "$DEV_APP_ID" "$b"
    done
else
    echo "   (no DEV_APP_ID -> ad-hoc signing, fine for your own Mac)"
    for b in "$WORK/root-au/$NAME.component" "$WORK/root-vst3/$NAME.vst3"; do
        codesign --force --deep --sign - "$b"
    done
fi

# After install, make macOS rescan Audio Units so Logic sees the plugin right away
cat > "$WORK/scripts-au/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
EOF
chmod +x "$WORK/scripts-au/postinstall"

make_component_pkg () {   # $1=root dir  $2=install location  $3=id suffix  $4=extra args
    local plist="$WORK/$3.plist"
    pkgbuild --analyze --root "$1" "$plist"
    # Always install to the fixed folder (don't "relocate" to an old copy somewhere else)
    plutil -replace 0.BundleIsRelocatable -bool NO "$plist"
    pkgbuild --root "$1" --component-plist "$plist" \
             --identifier "$BUNDLE_ID.$3" --version "$VERSION" \
             --install-location "$2" $4 "$WORK/pkgs/$3.pkg"
}

echo "==> Building component packages"
make_component_pkg "$WORK/root-au"   "/Library/Audio/Plug-Ins/Components" au   "--scripts $WORK/scripts-au"
make_component_pkg "$WORK/root-vst3" "/Library/Audio/Plug-Ins/VST3"       vst3 ""

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$NAME $VERSION</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="au"/>
        <line choice="vst3"/>
    </choices-outline>
    <choice id="au" title="Audio Unit (Logic Pro, GarageBand)" description="Installs to /Library/Audio/Plug-Ins/Components">
        <pkg-ref id="$BUNDLE_ID.au"/>
    </choice>
    <choice id="vst3" title="VST3 (Ableton, FL Studio, Cubase...)" description="Installs to /Library/Audio/Plug-Ins/VST3">
        <pkg-ref id="$BUNDLE_ID.vst3"/>
    </choice>
    <pkg-ref id="$BUNDLE_ID.au"   version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="$BUNDLE_ID.vst3" version="$VERSION">vst3.pkg</pkg-ref>
</installer-gui-script>
EOF

echo "==> Building installer"
SIGN_ARGS=()
[ -n "$DEV_INSTALLER_ID" ] && SIGN_ARGS=(--sign "$DEV_INSTALLER_ID" --timestamp)
productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" \
             "${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"}" "$PKG_OUT"

if [ -n "$NOTARY_PROFILE" ] && [ -n "$DEV_INSTALLER_ID" ]; then
    echo "==> Notarizing (takes a few minutes)"
    xcrun notarytool submit "$PKG_OUT" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG_OUT"
fi

echo ""
echo "Done: $PKG_OUT"
