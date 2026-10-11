#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DIR=$(mktemp -d)
trap 'rm -rf "$DIR"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/userspace/public/libs/libicwmp_dm/src" \
    "$ROOT/userspace/public/libs/libicwmp_dm/src/dm_model_policy.c" \
    "$ROOT/gpt_dev_note/tests/test_dm_model_policy.c" \
    -o "$DIR/test_dm_model_policy"
"$DIR/test_dm_model_policy"
