# AmiAPI

A small C foundation for accessing modern Internet APIs from classic AmigaOS.

## Baseline

- AmigaOS 3.x
- Motorola 68020+
- No FPU or MMU requirement
- `bsdsocket.library` compatible TCP/IP stack
- Static library first
- AmiSSL planned for HTTPS

## M1.1

M1.1 adds an AmigaOS transport backend alongside the already tested POSIX
backend. HTTP remains platform-independent.

### Host build

```sh
make clean
make PLATFORM=posix
make PLATFORM=posix test
```

### Amiga cross-build

A working AmigaOS cross toolchain and NDK/networking headers are required.

Typical shape:

```sh
make clean
make PLATFORM=amiga \
  CC=m68k-amigaos-gcc \
  AR=m68k-amigaos-ar \
  CFLAGS="-O2 -m68020 -Wall -Wextra"
```

Toolchain naming and include/library paths vary. M1.1 deliberately does not
pretend that a particular cross toolchain is already qualified.

The Amiga backend opens `bsdsocket.library` version 4 and uses its classic
socket API. This is intended for stacks such as Roadshow, Miami/MiamiDx and
Genesis/AmiTCP that provide the compatible interface.

## Current HTTP limitations

- plain `http://` only
- no redirects
- no chunked transfer decoding
- no streaming callbacks
- response body buffered with a configurable size limit

HTTPS/AmiSSL is the next major layer after the Amiga TCP backend is built and
run on the target.
