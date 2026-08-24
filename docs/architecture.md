# AmiAPI Architecture

## Purpose

AmiAPI provides a small reusable C layer for native Amiga applications that
need to consume modern Internet APIs without embedding a web browser.

## Initial target

The native baseline is:

- AmigaOS 3.x
- 68020+
- no required FPU
- no required MMU
- TCP/IP through a bsdsocket.library-compatible stack
- TLS through AmiSSL once HTTPS support is implemented

A stock A1200 with Fast RAM is the reference performance class.

## Design principles

1. Keep memory use bounded.
2. Prefer streaming over whole-response buffering.
3. Avoid mandatory large runtime dependencies.
4. Keep service-specific code outside the core.
5. Keep the public API small and C-friendly.
6. Do not freeze a shared-library ABI prematurely.
7. Make errors explicit and diagnosable.
8. Keep a future lightweight 68000/bridge transport possible without making it
   part of the v0.1 baseline.

## Layering

Future layers are expected to look approximately like:

    Application
        |
    AmiAPI public API
        |
    HTTP / URL / JSON / authentication helpers
        |
    TLS abstraction
        |
    bsdsocket.library-compatible networking

M0 implements only context lifecycle, errors, versioning, and the static build.
