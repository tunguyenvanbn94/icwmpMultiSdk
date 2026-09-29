#!/bin/sh
# Keep only the named SDKs, delete every other sdk/<name>/ directory, then
# regenerate sdk/enabled.*.  This is the supported way to hand the tree to
# somebody who must not see the other vendors' code.
#
#   ./tools/sdk-prune.sh mtk            # ship the MTK build only
#   ./tools/sdk-prune.sh bdk uci        # keep two
#   ./tools/sdk-prune.sh --list         # what is in the tree now
#
# After pruning: autoreconf -fi && ./configure --with-sdk=<kept>
set -e

cd "$(dirname "$0")/.."

list() {
	for d in sdk/*/; do
		n=${d#sdk/}; n=${n%/}
		[ -f "sdk/$n/sdk.mk" ] || continue
		printf '%s\n' "$n"
	done
}

[ "$1" = "--list" ] && { list; exit 0; }
[ -n "$1" ] || { echo "usage: $0 <sdk> [<sdk> ...]   (or --list)" >&2; exit 1; }

for keep in "$@"; do
	[ -f "sdk/$keep/sdk.mk" ] || { echo "$0: no SDK named '$keep' in sdk/" >&2; exit 1; }
done

for n in $(list); do
	drop=1
	for keep in "$@"; do
		[ "$n" = "$keep" ] && drop=0
	done
	[ "$drop" = 1 ] || continue
	echo "removing sdk/$n"
	rm -rf "sdk/$n"
done

./tools/sdk-scan.sh
echo
echo "Now: autoreconf -fi && ./configure --with-sdk=$1"
