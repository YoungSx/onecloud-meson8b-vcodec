#!/bin/sh
# Codec load alongside the box's production services.
#
# Runs soak.sh for MINUTES while probing every service the operator depends on,
# so a codec regression that only shows up under contention has a witness.
# Probes are read-only and unauthenticated:
#
#   OpenList        GET :5245/           (container, host network)
#   SublinkPro      GET :8000/
#   VoCat           GET :7575/
#   Home Assistant  GET :8123/
#   Arcane agent    no host port: count its own incoming-health-check log
#                   lines, which only advance while it is serving
#
# Exits non-zero if any service stops answering or the soak fails.
set -u
D=/root/venc-deploy
LOG=$D/p6/service-coexist.log
MINUTES=${1:-20}
SAMPLE=${2:-15}
FAILS=0

: > "$LOG"
say() { echo "$(date -Iseconds) $*" >> "$LOG"; }

code() {
	curl -s -o /dev/null -w '%{http_code}' --max-time 5 "$1" 2>/dev/null
}

arcane_hits() {
	docker logs arcane-agent 2>&1 | grep -c 'Incoming request' || true
}

probe() {
	# Prints "name=code" for the four HTTP services; Arcane is checked by the
	# caller against its previous log-line count.
	printf 'openlist=%s sublink=%s vocat=%s ha=%s' \
		"$(code http://127.0.0.1:5245/)" \
		"$(code http://127.0.0.1:8000/)" \
		"$(code http://127.0.0.1:7575/)" \
		"$(code http://127.0.0.1:8123/)"
}

bad() {
	# $1 is a probe string; any 000 (or empty) is an unreachable service.
	case " $1 " in
	*"=000"*|*"= "*) return 0 ;;
	esac
	return 1
}

base=$(probe)
base_arc=$(arcane_hits)
say "== coexist start minutes=$MINUTES sample=${SAMPLE}s baseline: $base arcane_hits=$base_arc"
if bad "$base"; then
	say "FAIL baseline: a service is already down before any codec load"
	exit 1
fi

"$D/soak.sh" "$MINUTES" >/dev/null 2>&1 &
soak=$!

start=$(date +%s)
end=$((start + MINUTES * 60))
n=0
while kill -0 "$soak" 2>/dev/null && [ "$(date +%s)" -lt $((end + 300)) ]; do
	n=$((n + 1))
	p=$(probe)
	arc=$(arcane_hits)
	if bad "$p"; then
		say "sample=$n DOWN $p arcane_hits=$arc"
		FAILS=$((FAILS + 1))
	else
		say "sample=$n $p arcane_hits=$arc"
	fi
	sleep "$SAMPLE"
done
wait "$soak"
soak_rc=$?

final=$(probe)
final_arc=$(arcane_hits)
if bad "$final"; then
	say "FAIL final: $final"
	FAILS=$((FAILS + 1))
fi
if [ "$final_arc" -le "$base_arc" ] && [ "$MINUTES" -ge 2 ]; then
	say "FAIL arcane: log-line count did not advance ($base_arc -> $final_arc)"
	FAILS=$((FAILS + 1))
fi
if [ "$soak_rc" -ne 0 ]; then
	say "FAIL soak exited $soak_rc"
	FAILS=$((FAILS + 1))
fi

say "COEXIST_DONE samples=$n fails=$FAILS soak_rc=$soak_rc final: $final arcane_hits=$final_arc"
[ "$FAILS" -eq 0 ]