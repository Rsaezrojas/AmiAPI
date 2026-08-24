# AmiAPI Architecture

## Native baseline

- AmigaOS 3.x
- 68020+
- no FPU
- no MMU
- bsdsocket.library-compatible networking

A stock A1200 with Fast RAM is the reference performance class.

## Transport boundary

HTTP is independent of the operating-system socket API:

    Application
        |
      AmiAPI
        |
       HTTP
        |
  transport interface
     /         \
 POSIX       AmigaOS
 sockets    bsdsocket.library

M1.1 provides both backends. The POSIX backend remains valuable for rapid
development and deterministic host-side tests. The Amiga backend is selected
with `PLATFORM=amiga`.

The TLS milestone will extend this boundary rather than putting AmiSSL calls
throughout HTTP code.

## Resource ownership

The Amiga backend owns its `bsdsocket.library` handle. M1.1 performs platform
cleanup when an AmiAPI context is destroyed. Multiple simultaneous contexts
are not yet a supported/qualified use case; resource lifetime will be refined
before the public ABI is frozen.

## Memory

HTTP currently buffers response bodies but enforces a default 1 MiB bound.
Streaming/download-to-file becomes the preferred mechanism in M3.
