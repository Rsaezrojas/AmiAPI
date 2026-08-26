# AmiAPI Roadmap

## Completed foundation

### Native Amiga transport

- 68020 AmigaOS cross-build
- bsdsocket.library transport
- heap-backed receive buffer for small Amiga process stacks
- bounded response buffering
- POSIX development/test transport

### HTTP correctness

- Content-Length handling
- chunked transfer decoding
- redirects
- request methods and bodies
- custom headers
- redirect-sensitive header handling

### HTTPS / AmiSSL

- native AmiSSL transport
- certificate-chain verification
- hostname verification
- SNI
- HTTP to HTTPS redirects
- qualified FS-UAE/AmiSSL test profile

### API helpers

- JSON parser
- JSON query/value helpers
- JSON HTTP helpers
- URL percent encoding
- query builder
- form encoding
- Basic authentication
- Bearer authentication

### Network hardening

- configurable per-context network timeout
- TCP connection timeout
- HTTP send/receive timeout
- TLS handshake/read/write timeout

DNS resolver timeout remains controlled by the platform TCP/IP stack.

### Streaming and download-to-file

- incremental decoded response-body callbacks
- bounded-memory downloads using the shared HTTP body decoder
- binary-safe Content-Length, chunked, EOF-framed, HTTP, and HTTPS downloads
- sibling temporary files published only after successful 2xx completion
- decoded maximum-body enforcement and cleanup on failure
- conservative preservation of existing destination files on POSIX and AmigaOS

## Next milestones

### SDK stabilization

- public API review
- error model review
- documentation
- examples
- compatibility qualification
- release packaging

### First real API client

Open-Meteo remains the small reference API integration.

### SampleScout

Freesound-based native Amiga client built on AmiAPI.
