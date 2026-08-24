# AmiAPI

A small C foundation for accessing modern Internet APIs from classic AmigaOS.

## Baseline

- AmigaOS 3.x
- Motorola 68020 or newer
- No FPU requirement
- No MMU requirement
- Static library first; shared-library ABI comes later
- C89-friendly implementation style

M0 deliberately contains no networking, TLS, or JSON support. It establishes a
small buildable API and repository structure before those layers are added.

## Build on a development host

```sh
make
./build/hello
```

Clean with:

```sh
make clean
```

## Cross compilation

The Makefile is intentionally simple. Override the compiler and archiver when a
working Amiga cross toolchain is available, for example:

```sh
make clean
make CC=m68k-amigaos-gcc AR=m68k-amigaos-ar \
     CFLAGS="-O2 -m68020 -Wall -Wextra -Iinclude"
```

Exact cross-toolchain flags will be qualified in a later milestone.

## Project status

M0: repository/build foundation.
