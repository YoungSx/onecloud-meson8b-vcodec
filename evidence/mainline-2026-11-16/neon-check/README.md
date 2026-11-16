# NEON body equivalence check

`neon.c` is the body of `drivers/staging/media/meson/vdec/meson8b-swap-neon.c`
with system headers instead of kernel ones, so it builds on the host. `main.c`
runs the body over the sizes the
driver can pass — zero, one step, a step and a half, and a 1080p plane — and
compares each against `__builtin_bswap64`.

```
cc -O2 -Wall -Wextra -Werror neon.c main.c -o neoncheck && ./neoncheck
```

This pins the intrinsic body only. It says nothing about the FPU flags the
kernel actually compiles the file with, which is why the module build is
recorded next to it.
