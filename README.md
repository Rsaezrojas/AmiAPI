# AmiAPI

A small native HTTP/HTTPS and API client library for classic AmigaOS.

Current version: 0.1.2

## Target

- AmigaOS 3.x
- 68020+
- no FPU required
- no MMU required
- bsdsocket.library-compatible TCP/IP stack
- AmiSSL 5 for HTTPS

A stock A1200 with Fast RAM is the reference performance class.

## Current features

- HTTP and HTTPS
- TLS certificate and hostname verification
- HTTP redirects
- Content-Length handling
- chunked transfer decoding
- generic HTTP request methods and request bodies
- custom request headers
- sensitive-header stripping across cross-origin redirects
- Basic authentication
- Bearer authentication
- URL percent encoding and query construction
- application/x-www-form-urlencoded helpers
- JSON parsing and JSON HTTP helpers
- bounded response buffering
- incremental response-body callbacks
- safe streaming downloads to files
- configurable network timeout
- POSIX backend for host-side development and testing
- native AmigaOS bsdsocket.library/AmiSSL backend

## Network timeout

Each AmiAPI context has a configurable network timeout:

    AmiAPI_SetTimeout(ctx, seconds);

The value can be queried with:

    AmiAPI_Timeout(ctx);

The timeout applies to:

- TCP connection establishment
- socket reads
- socket writes
- TLS handshake I/O
- TLS reads and writes

A value of zero disables AmiAPI's timeout and allows the underlying
network operation to wait indefinitely.

DNS resolution is currently performed by the platform resolver before
TCP connection establishment. Resolver calls may therefore use the
TCP/IP stack's own DNS timeout and are not bounded by
AmiAPI_SetTimeout().

## Response bodies and file downloads

Responses are buffered by default. Applications can instead configure an
`AmiAPIBodyCallback` to consume decoded body bytes incrementally, or download
directly to a file:

    error = AmiAPI_RequestDownloadFile(request, "Work:response.bin");

`AmiAPI_DownloadFile()` is the corresponding simple GET convenience helper.
Downloads use the same Content-Length, chunked, EOF-framed, redirect, timeout,
and TLS path as other requests. The whole file is never held in memory.

Downloads are published only for a final 2xx HTTP response after the complete
body has been received and the temporary file has closed successfully. AmiAPI
writes a suffixed temporary file beside the destination, then renames it into
place. Failures remove the temporary file. The configured maximum-body limit
still applies to total decoded bytes.

For portable safety, an existing destination is never replaced or unlinked;
the download fails and preserves it. This conservative policy avoids relying
on differing replacement semantics across POSIX and classic Amiga filesystems.
Temporary-name creation uses portable C file operations, so applications
should not concurrently download to the same destination path.

The receive scratch buffer is heap-backed to remain safe with the small
process stacks commonly used on classic AmigaOS.

## Building

POSIX development build:

    make PLATFORM=posix

Run the host-side regression suite:

    make PLATFORM=posix test

Classic AmigaOS cross-build:

    make PLATFORM=amiga

The Amiga build targets 68020 by default.

## Transport architecture

Protocol handling is kept independent of the platform socket API:

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
               +
             AmiSSL

This keeps most protocol behaviour testable on the development host
while retaining a native classic-Amiga transport.
