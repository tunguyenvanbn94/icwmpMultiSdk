#!/bin/sh
# Keep only the named SDKs in BOTH components of this overlay and regenerate
# their sdk/enabled.* files.  Use it before handing the tree to somebody who
# must only see one SDK.
#
#   ./sdk-prune.sh mtk          # ship the MTK build only
#   ./sdk-prune.sh bdk          # ship the Broadcom build only
#   ./sdk-prune.sh --list
#
# After pruning, both components still configure and build:
#   libtr098: ./configure --with-sdk=<name>
#   icwmp:    ./configure --enable-icwmp_tr098 --with-sdk=<name>
set -e

cd "$(dirname "$0")"
LIB=public/libs/libtr098/libtr098
APP=public/apps/icwmp/icwmp

for c in "$LIB" "$APP"; do
	[ -x "$c/tools/sdk-prune.sh" ] || { echo "$0: $c/tools/sdk-prune.sh missing" >&2; exit 1; }
done

if [ "$1" = "--list" ]; then
	for c in "$LIB" "$APP"; do
		echo "$c:"
		(cd "$c" && ./tools/sdk-prune.sh --list | sed 's/^/  /')
	done
	exit 0
fi

[ -n "$1" ] || { echo "usage: $0 <sdk> [<sdk> ...]   (or --list)" >&2; exit 1; }

for c in "$LIB" "$APP"; do
	echo "== $c"
	(cd "$c" && ./tools/sdk-prune.sh "$@")
done

echo
echo "Left in the tree:"
"$0" --list
