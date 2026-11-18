#!/bin/sh
# Swap the codec modules on the target between the installed release and a
# development build, for hardware tests during development. Firmware and the
# installed package stay untouched; "release" restores the service.
#
#   dev-swap.sh dev DIR/meson-vdec.ko [DIR/meson-venc.ko]
#   dev-swap.sh release
#
# The development package is the installed package with the given modules
# replaced, so runtime-device.py loads it with the same checks.
set -eu
installed=/opt/meson8b-vcodec/current
dev=/root/venc-deploy/dev/package
tool=$installed/scripts/runtime-device.py

in_use() {
	for node in /dev/video*; do
		[ -e "$node" ] || continue
		if fuser -s "$node" 2>/dev/null; then
			echo "$node is open:" >&2
			fuser -v "$node" >&2 || true
			return 0
		fi
	done
	return 1
}

case ${1:-} in
dev)
	shift
	[ $# -ge 1 ] || { echo 'usage: dev-swap.sh dev MODULE.ko...' >&2; exit 2; }
	! in_use || exit 1
	rm -rf "$dev"
	mkdir -p "$(dirname "$dev")"
	cp -a "$(readlink -f "$installed")" "$dev"
	rm -f "$dev/SHA256SUMS"
	for module do
		install -m 644 "$module" "$dev/modules/$(basename "$module")"
	done
	(cd "$dev" && find . -type f -printf '%P\0' | sort -z | xargs -0 sha256sum) > "$dev.sums"
	mv "$dev.sums" "$dev/SHA256SUMS"
	systemctl stop meson8b-vcodec.service
	python3 "$tool" load "$dev"
	echo "development modules loaded: $(md5sum "$dev"/modules/*.ko | awk '{print $1}' | tr '\n' ' ')"
	;;
release)
	! in_use || exit 1
	python3 "$tool" unload
	systemctl start meson8b-vcodec.service
	systemctl is-active meson8b-vcodec.service
	;;
*)
	echo 'usage: dev-swap.sh dev MODULE.ko... | release' >&2
	exit 2
	;;
esac
ls /dev/video*
