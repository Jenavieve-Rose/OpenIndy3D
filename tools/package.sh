#!/bin/sh
# package: signs OpenIndy3D.app for distribution and packs it in a disk image (PORT-PLAN.md P8.13).
#
#   tools/package.sh [<build-dir>] [<out-dir>]     (defaults: build, dist)
#
# Environment:
#   J3D_SIGN_IDENTITY   the signing identity, "Developer ID Application: <name> (<team>)"; default "-", an ad-hoc
#                       signature, which runs here but which Gatekeeper refuses on other Macs
#   J3D_NOTARY_PROFILE  a notarytool keychain profile (xcrun notarytool store-credentials <profile> ...); when set,
#                       the disk image is submitted to Apple's notary service, and stapled once accepted
#
# The app is signed with the Hardened Runtime and a secure timestamp (entitlements:
# platform/apple/bundle/OpenIndy3D.entitlements), verified, packed with a link to /Applications and the README, and
# the disk image is signed too. Builds nothing: run it after cmake --build on a Release (or RelWithDebInfo) tree.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$root/build}
out=${2:-$root/dist}
identity=${J3D_SIGN_IDENTITY:--}
app="$build/OpenIndy3D.app"

[ -d "$app" ] || { echo "package: no $app; build the apple tree first" >&2; exit 1; }

version=$(/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$app/Contents/Info.plist")
name="OpenIndy3D-$version"
stage="$out/stage"
rm -rf "$stage" "$out/$name.dmg"
mkdir -p "$stage"

# Nothing from outside the bundle but the system's own libraries (P8.3)
if otool -L "$app/Contents/MacOS/OpenIndy3D" | tail -n +2 | grep -v -E "^\s+(/System/Library/|/usr/lib/)"; then
    echo "package: the app links a library outside the system (above)" >&2
    exit 1
fi

if cmp -s "$app/Contents/Resources/OpenIndy3D.icns" "$build/OpenIndy3D.icns" && \
   grep -q "indiana_jones_and_the_infernal_machine" "$build/CMakeCache.txt"; then
    echo "package: warning: the icon is made from the retail box art (J3D_ICON_SOURCE); give an original picture for a public release" >&2
fi

ditto "$app" "$stage/OpenIndy3D.app"
xattr -cr "$stage/OpenIndy3D.app"

timestamp=--timestamp
[ "$identity" = "-" ] && timestamp=--timestamp=none
codesign --force --options runtime $timestamp --entitlements "$root/platform/apple/bundle/OpenIndy3D.entitlements" \
    --sign "$identity" "$stage/OpenIndy3D.app"
codesign --verify --deep --strict --verbose=2 "$stage/OpenIndy3D.app"

ln -s /Applications "$stage/Applications"
cp "$root/README.md" "$stage/README.md"
hdiutil create -quiet -volname "OpenIndy3D $version" -srcfolder "$stage" -fs HFS+ -format UDZO "$out/$name.dmg"
codesign --force $timestamp --sign "$identity" "$out/$name.dmg"
rm -rf "$stage"

if [ -n "${J3D_NOTARY_PROFILE:-}" ]; then
    [ "$identity" != "-" ] || { echo "package: notarization needs a Developer ID identity (J3D_SIGN_IDENTITY)" >&2; exit 1; }
    xcrun notarytool submit "$out/$name.dmg" --keychain-profile "$J3D_NOTARY_PROFILE" --wait
    xcrun stapler staple "$out/$name.dmg"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$out/$name.dmg"
else
    echo "package: not notarized (set J3D_NOTARY_PROFILE)"
fi

echo "package: $out/$name.dmg"
