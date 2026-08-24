# AmiAPI Roadmap

## M0 - Foundation

- Repository structure
- Static C library
- Opaque context
- Error namespace
- Version API
- Minimal example
- Host build

## M1 - TCP and HTTP

- bsdsocket-compatible transport abstraction
- HTTP GET
- response status and headers
- bounded reads and explicit limits
- host-side test transport where practical

## M2 - HTTPS

- AmiSSL integration
- certificate verification
- TLS error mapping
- HTTPS GET

## M3 - Streaming HTTP

- response streaming callbacks
- download-to-file support
- redirects
- size limits and cancellation

## M4 - JSON

- small-footprint parser
- streaming/event-oriented operation where practical
- UTF-8 handling policy

## M5 - First real API

- Open-Meteo example
- query/URL encoding
- real HTTPS + JSON demonstration

## M6 - SDK preview

- API cleanup
- documentation
- examples
- cross-build qualification on AmigaOS
- initial Aminet-style package layout

## M7 - SampleScout foundation

- Freesound provider built on AmiAPI
- search and metadata
- preview/download plumbing
- authentication according to Freesound's approved client model
