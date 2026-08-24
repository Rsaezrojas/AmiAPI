# AmiAPI

A small C foundation for accessing modern Internet APIs from classic AmigaOS.

## Baseline

- AmigaOS 3.x
- Motorola 68020+
- No FPU or MMU requirement
- Static library first
- Bounded-memory design
- AmiSSL planned for HTTPS

## M1 status

M1 introduces the first HTTP/1.1 client and a transport boundary.

The current transport implementation is POSIX and is used for development and
integration testing on the host. The AmigaOS `bsdsocket.library` backend is the
next platform backend to qualify.

Supported in this milestone:

- `http://` URLs
- DNS lookup
- TCP connect/send/receive
- HTTP/1.1 GET
- status parsing
- response headers
- case-insensitive header lookup
- bounded response bodies
- deterministic local integration test

Not supported yet:

- HTTPS/TLS
- redirects
- chunked transfer decoding
- keep-alive reuse
- IPv6 literals in URLs
- streaming response callbacks

## Build and test

```sh
make clean
make
make test
```

Try against a plain HTTP endpoint:

```sh
./build/httpget http://example.com/
```
