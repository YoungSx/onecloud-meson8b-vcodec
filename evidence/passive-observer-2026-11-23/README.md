# Passive outage evidence and storage capacity

Continues the [two-outage investigation](../outage-investigation-2026-11-22/README.md).
The second outage remains undetermined. This pass uses normal production
activity, with no codec load experiment, clock change, network reconfiguration
or boot change. Running business services were not restarted; the already stopped
Arcane agent was started after reclaiming duplicate archives.

## Implemented and deployed

[`observe-network.py`](../../scripts/observe-network.py) samples gateway reachability,
carrier, cumulative Ethernet and IRQ/softirq counters, memory/swap, blocked tasks,
disk I/O and free capacity/inodes. Missing metrics and failed probes remain explicit.
Every JSON line and rotation directory entry is synced; two logs retain at most
8 MiB with mode 0600. SIGTERM stops collection without recovery/reboot actions.
The tool uses the target's Python 3.8 and standard library plus the existing ping.

[Bounded live check](bounded-live.json): four samples over 30.002 seconds;
all four gateway replies and carrier checks pass, RX/TX counters advance by
623/496 packets, and RX/TX error counters do not increase. Four service HTTP
checks return 200 and the codec service stays active. Boot ID is unchanged.
The 35-second check exits normally. A separate six-hour transient unit then starts
with a ten-second interval and a RuntimeMaxSec limit; its raw records stay private
on the system partition. This is an initial deployment check, **not a six-hour
completed test or a stability guarantee**. See the
[operator instructions](../../docs/troubleshooting.md#正常运行时保留失联现场).

Host checks: all 57 Python tests, existing C sanitizer tests, ShellCheck and local
documentation links pass. Six new tests cover rotation/permissions, symlinks,
missing metrics, probe failures, free/reserved space and graceful termination.
Deployed script SHA256 is
`4f872c2638d544c1840fba8a7732c313ff1586c88b5ffd82a358261b370abc40`.

## New capacity finding

Creating the first observer directory on the data SSD failed with ENOSPC.
The approximately 109.46 GiB filesystem had zero ordinary available bytes and
only 16 MiB free including reserved blocks; inode use was approximately 4%.
The downloads directory accounts for approximately 79 GiB. This is a **current
capacity problem**, not evidence that storage caused the earlier network outages.

Ten duplicate project archives were removed in two batches after all SHA256/size comparisons
matched copies retained on the build host and none were held open by a process.
[Hashes and sizes](reclaimed-duplicates.json) document 1646747597 bytes
(approximately 1.53 GiB) reclaimed. The archives were old image/support/source
transfers, outside active container bind mounts. Original driver/firmware/service
backups and current installations remain intact. Business downloads and Docker
data were not cleaned. Retained build-host copies allow restoration.

The first batch reclaimed about 912 MiB, but ordinary available space still
reported zero because free blocks were reserved. After the second batch, ordinary
available space recovers to approximately 426 MiB. Capacity pressure remains high.
No reserved-block or mount settings were changed. The observer uses
the system partition, which had approximately 1.21 GiB ordinarily available at
acceptance, and records the SSD's capacity separately with `--storage`.

Arcane agent had exited at 03:03 UTC; Docker recorded an ENOSPC failure writing
its restart-time resolv.conf hash. After the first cleanup, `docker start` restores
it at 04:37 UTC, and it remains running at the
[final check](final-health.json) at 04:45 UTC. Four HTTP endpoints and the codec
service remain healthy. The passive observer has 81 successful gateway samples
at that check. This establishes a current storage-related service failure and
recovery, not the cause of either earlier LAN outage.

## Boundaries

The previous temporary kernel-log reader is no longer running; its last log write
was 2026-11-22 16:20:51 UTC. No evidence establishes why it stopped. This new
observer records counters, not kernel stacks or clock registers. Logs stopping
could mean a host hang, reader exit or storage stall. Serial/crash evidence and an
independent LAN observer remain necessary to distinguish those cases. Sustained
production load tests remain paused.
