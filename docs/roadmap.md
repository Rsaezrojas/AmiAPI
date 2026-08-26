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

### SDK/API hardening

- final-response header lookup with documented ownership and duplicate policy
- decoded-body progress for buffered, streamed, and downloaded responses
- cooperative callback cancellation with safe download cleanup
- complete public error-string coverage
- focused API review for the first real application

## Next milestones

### AmiWeather

Native Amiga weather client using the Open-Meteo API. AmiWeather will be the
first real application to exercise the hardened SDK.

### AmiFreesound

Native Amiga Freesound client, planned after AmiWeather and other small
real-world SDK consumers.
