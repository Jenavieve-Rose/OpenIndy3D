#!/bin/sh
# make_icns: builds the app icon (PORT-PLAN.md P8.2) from a square picture, at least 1024 by 1024, with sips and
# iconutil. CMakeLists.txt runs it when J3D_ICON_SOURCE names a picture; the picture is not kept in the repository.
#
#   tools/make_icns.sh <picture> <out.icns>
set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 <picture> <out.icns>" >&2
    exit 2
fi

src=$1
out=$2
work=$(mktemp -d "${TMPDIR:-/tmp}/make_icns.XXXXXX")
trap 'rm -rf "$work"' EXIT
set_dir="$work/icon.iconset"
mkdir "$set_dir"

# The sizes iconutil expects: 16 to 512 points, each at 1x and 2x
for size in 16 32 128 256 512; do
    sips -s format png -z $size $size "$src" --out "$set_dir/icon_${size}x${size}.png" > /dev/null
    double=$((size * 2))
    sips -s format png -z $double $double "$src" --out "$set_dir/icon_${size}x${size}@2x.png" > /dev/null
done

iconutil -c icns "$set_dir" -o "$out"
