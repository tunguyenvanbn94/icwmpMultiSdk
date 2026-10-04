# Common settings of the host test, sourced by the other scripts.
HOST_DIR=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HOST_DIR/../.." && pwd)
WORK=${ICWMP_HOST_WORK:-/tmp/icwmp-host}
PREFIX=$WORK/prefix
LIB_SRC=$REPO/userspace/public/libs/libicwmp_dm/src
APP_SRC=$REPO/userspace/public/apps/icwmp/icwmp
MICROXML_SRC=$REPO/userspace/public/libs/microxml/microxml
MATRIX=$REPO/docs/issue/tr098_coverage_matrix.tsv
BIN=${ICWMP_HOST_BIN:-$WORK/build/app/bin/icwmp_tr098d}
UBUS="$PREFIX/bin/ubus -s /var/run/ubus/ubus.sock"
RUN=$WORK/run
mkdir -p "$RUN" 2>/dev/null
