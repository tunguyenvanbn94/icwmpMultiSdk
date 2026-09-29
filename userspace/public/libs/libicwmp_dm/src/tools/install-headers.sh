#!/bin/sh
# A1: install the existing header surface under a model-neutral namespace.
# Legacy libtr098 headers forward to it while the library ABI remains unchanged.
# Model-specific header manifests/pruning are a separate A2 change.
set -eu

[ "$#" = 2 ] || { echo "usage: $0 <include-root> <sdk>" >&2; exit 1; }
INCLUDE_ROOT=$1
SDK=$2
SOURCE_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$SDK" in
    ''|*[!a-z0-9_]*) echo "invalid SDK name: $SDK" >&2; exit 1 ;;
esac
[ -f "$SOURCE_ROOT/sdk/$SDK/sdk.mk" ] || {
    echo "SDK not present: $SDK" >&2; exit 1;
}
[ -n "$INCLUDE_ROOT" ] && [ "$INCLUDE_ROOT" != / ] || exit 1
# Both namespaces are owned by this library. Do not retain headers of a
# previously selected SDK in staging after installing a different profile.
rm -rf -- "$INCLUDE_ROOT/icwmp_dm" "$INCLUDE_ROOT/libtr098"
mkdir -p "$INCLUDE_ROOT/icwmp_dm/sdk" "$INCLUDE_ROOT/libtr098/sdk"

install_header() {
    source=$1
    relative=$2
    install -m 0644 "$source" "$INCLUDE_ROOT/icwmp_dm/$relative"
    printf '/* Transitional include name. Use <icwmp_dm/%s>. */\n#include <icwmp_dm/%s>\n' \
        "$relative" "$relative" > "$INCLUDE_ROOT/libtr098/$relative"
    chmod 0644 "$INCLUDE_ROOT/libtr098/$relative"
}

for file in "$SOURCE_ROOT"/*.h "$SOURCE_ROOT"/tr098/*.h \
            "$SOURCE_ROOT"/tr098/common/*.h "$SOURCE_ROOT/sdk/$SDK"/*.h \
            "$SOURCE_ROOT/sdk/$SDK/dm098"/*.h; do
    [ -f "$file" ] || continue
    install_header "$file" "${file##*/}"
done
install_header "$SOURCE_ROOT/sdk/sdk.h" sdk/sdk.h
