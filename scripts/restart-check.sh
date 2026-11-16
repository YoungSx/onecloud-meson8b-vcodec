#!/bin/sh
# Does `systemctl restart` bring the codec stack back on its own?
#
# Only a cold boot after an unplanned reset had been accepted. A restart is a
# different path: the oneshot unit runs ExecStop (runtime-device.py unload,
# which removes the overlay and unloads three modules) and then ExecStart
# (load) in one transaction, with no reboot to reset the hardware in between.
# That is the case where a stale state can survive.
#
# Refuses to run if anything holds /dev/video*, because the unload would pull
# the modules out from under a client. Records the dmesg position so the driver
# messages of this one restart are separable from the rest of the log.
set -u
D=/root/venc-deploy/p6
LOG=$D/restart.log
: > "$LOG"
say() { echo "$(date -Iseconds) $*" >> "$LOG" && echo "$*"; }
# Both nodes or neither; a missing one is the failure being looked for.
nodes() {
	for d in /dev/video0 /dev/video1; do
		[ -e "$d" ] && printf '%s ' "$d" || printf '!%s ' "$d"
	done
}

hold=$(fuser /dev/video0 /dev/video1 2>/dev/null || true)
if [ -n "$hold" ]; then
	say "!! /dev/video* held by: $hold"
	say "RESTART_CHECK_ABORTED"
	exit 1
fi

before=$(dmesg | wc -l)
say "state before: $(systemctl is-active meson8b-vcodec) $(systemctl is-enabled meson8b-vcodec)"
say "nodes before: $(nodes)"

t0=$(date +%s)
systemctl restart meson8b-vcodec
rc=$?
t1=$(date +%s)
say "restart rc=$rc elapsed=$((t1 - t0))s"
say "state after: $(systemctl is-active meson8b-vcodec)"
say "show: $(systemctl show -p Result -p ExecMainStatus meson8b-vcodec | tr '\n' ' ')"

# Give the oneshot up to 5 s to settle, then the nodes must be back.
i=0
while [ $i -lt 5 ] && [ ! -e /dev/video0 ]; do
	sleep 1; i=$((i + 1))
done
say "nodes after: $(nodes)"

say "=== driver messages of this restart ==="
dmesg | tail -n +$((before + 1)) >> "$LOG"

# A restart that leaves the nodes but cannot decode is not a pass.
d=$D/geo/A-up.nv12
rm -f "$d"
FRAME_DUMP=$d timeout -k 5 120 /root/venc-deploy/res-switch-test /dev/video0 \
	$D/A-up.m4v MPG4 320 240 >> "$LOG" 2>&1
say "decode smoke frames=$(($(stat -c %s "$d" 2>/dev/null || echo 0) / 196608))"
gzip -1 -c "$d" > $D/geo/restart-smoke.nv12.gz 2>/dev/null && rm -f "$d"

say "RESTART_CHECK_DONE"
