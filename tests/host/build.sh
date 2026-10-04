#!/bin/sh
# Build libubox/uci/ubus (pinned), microxml, libtr098 (--with-sdk=mtk) and
# icwmp_tr098d on the host, under $ICWMP_HOST_WORK (default /tmp/icwmp-host).
# Re-run after editing the sources: only what changed is rebuilt.
set -e
. "$(dirname "$0")/env.sh"

LIBUBOX_REV=e7608b69283d919d031d13cc8e21692503f5dbea
UCI_REV=74f6277aabffc943d026f406df57c22595134c42
UBUS_REV=414c60a2e29e72c2c4f573bc50ad54aef076d169

for t in gcc make cmake autoreconf libtoolize pkg-config rsync python3 git; do
	command -v $t >/dev/null || { echo "missing tool: $t"; exit 1; }
done
for h in json-c/json.h curl/curl.h openssl/ssl.h zlib.h; do
	echo "#include <$h>" | gcc -E -x c - >/dev/null 2>&1 || { echo "missing header <$h> (libjson-c-dev libcurl4-openssl-dev libssl-dev zlib1g-dev)"; exit 1; }
done

mkdir -p "$WORK/src" "$WORK/build" "$PREFIX"

dep() {	# dep <name> <rev>
	d=$WORK/src/$1
	[ -d "$d/.git" ] || git clone -q "https://github.com/openwrt/$1.git" "$d"
	if [ "$(git -C "$d" rev-parse HEAD)" != "$2" ] || [ ! -f "$d/build/.done" ]; then
		git -C "$d" fetch -q origin 2>/dev/null || true
		git -C "$d" checkout -q "$2"
		rm -rf "$d/build"; mkdir -p "$d/build"
		(cd "$d/build" && cmake -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX" \
			-DCMAKE_INSTALL_RPATH="$PREFIX/lib" -DBUILD_LUA=OFF -DBUILD_EXAMPLES=OFF \
			-DBUILD_STATIC=OFF -DCMAKE_BUILD_TYPE=Debug .. >/dev/null 2>&1 && make -j"$(nproc)" >/dev/null && make install >/dev/null)
		touch "$d/build/.done"
		echo "built $1"
	fi
}
dep libubox $LIBUBOX_REV
dep uci $UCI_REV
dep ubus $UBUS_REV

if [ ! -f "$PREFIX/lib/libmicroxml.so" ]; then
	rm -rf "$WORK/build/microxml"; cp -r "$MICROXML_SRC" "$WORK/build/microxml"
	(cd "$WORK/build/microxml" && autoconf configure.in > configure 2>/dev/null && chmod +x configure &&
	 ./configure --prefix="$PREFIX" --disable-threads >/dev/null 2>&1 &&
	 make install > "$WORK/build/microxml.log" 2>&1) || { tail -5 "$WORK/build/microxml.log"; exit 1; }
	echo "built microxml"
fi

# copy the tree; touch what changed so make sees it (rsync keeps mtimes)
sync_tree() {	# sync_tree <src> <dst>
	mkdir -p "$2"
	(cd "$2" && rsync -rc --out-format=%n --exclude=.git "$1"/ ./ | grep -v '/$' | xargs -r touch)
}

CFLAGS_COMMON="-g -O1 -D_GNU_SOURCE -I$PREFIX/include"
LDFLAGS_COMMON="-L$PREFIX/lib -Wl,-rpath,$PREFIX/lib"

sync_tree "$LIB_SRC" "$WORK/build/dm"
if [ ! -f "$WORK/build/dm/Makefile" ]; then
	(cd "$WORK/build/dm" && ./tools/sdk-scan.sh >/dev/null && autoreconf -i >/dev/null 2>&1 &&
	 CFLAGS="$CFLAGS_COMMON" LDFLAGS="$LDFLAGS_COMMON" ./configure --prefix="$PREFIX" --with-sdk=mtk >/dev/null)
fi
make -C "$WORK/build/dm" -j"$(nproc)" > "$WORK/build/dm.log" 2>&1 || { grep -m5 error "$WORK/build/dm.log"; exit 1; }
cp -a "$WORK/build/dm"/bin/.libs/libtr098.so* "$PREFIX/lib/"
(cd "$WORK/build/dm" && ./tools/install-headers.sh "$PREFIX/include" mtk >/dev/null)

sync_tree "$APP_SRC" "$WORK/build/app"
if [ ! -f "$WORK/build/app/Makefile" ]; then
	(cd "$WORK/build/app" && ./tools/sdk-scan.sh >/dev/null && autoreconf -i >/dev/null 2>&1 &&
	 PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" CFLAGS="$CFLAGS_COMMON -I/usr/include/$(gcc -dumpmachine)" \
	 LDFLAGS="$LDFLAGS_COMMON" ./configure --prefix="$PREFIX" --enable-icwmp_tr098 --enable-http=curl \
		--enable-debug --with-sdk=mtk --with-uci-include-path="$PREFIX/include" \
		--with-libubox-include-path="$PREFIX/include" --with-libubus-include-path="$PREFIX/include" >/dev/null)
fi
make -C "$WORK/build/app" -j"$(nproc)" > "$WORK/build/app.log" 2>&1 || { grep -m5 error "$WORK/build/app.log"; exit 1; }
echo "ok: $BIN"
