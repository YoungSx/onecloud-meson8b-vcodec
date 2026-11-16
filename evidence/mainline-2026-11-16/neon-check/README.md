# NEON body equivalence check

The check used to live here as a hand-made copy of
`vdec/meson8b-swap-neon.c` plus a driver. The copy could drift from the file
that ships, so it moved into the host suite as
[`tests/test_swap_neon.c`](../../../tests/test_swap_neon.c): it `#include`s the
driver source itself and runs it over the sizes the driver can pass — zero,
one step, a step and a half, a 1080p plane — comparing each against
`__builtin_bswap64`. `make test` runs it wherever `arm_neon.h` exists and
skips it on a host without one.

`neon-check.log` is the run that was recorded here, kept as evidence of the
result rather than as a buildable tree.

This pins the intrinsic body only. It says nothing about the FPU flags the
kernel actually compiles the file with, which is why the module build is
recorded next to it.
