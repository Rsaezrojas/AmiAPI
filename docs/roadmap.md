# AmiAPI Roadmap

## M0 - Foundation — complete

## M1 - TCP + HTTP — complete on POSIX

## M1.1 - AmigaOS transport — implementation complete, target qualification pending
- platform-selectable transport backend
- bsdsocket.library v4 open/close
- IPv4 DNS via gethostbyname
- TCP connect/send/receive/close
- unchanged platform-independent HTTP layer
- next acceptance gate: cross-build and run `httpget` on AmigaOS

## M2 - HTTPS / AmiSSL
Only begin target integration after M1.1 has been built and exercised on the
Amiga target.

## M3 - Streaming HTTP
Callbacks, download-to-file, redirects, chunked transfer decoding, cancellation.

## M4 - JSON
Small-footprint parsing and UTF-8 policy.

## M5 - Open-Meteo
First complete modern HTTPS+JSON API example.

## M6 - SDK preview
Documentation, cross-build qualification and Aminet-style packaging.

## M7 - SampleScout
Freesound provider and first substantial AmiAPI application.
