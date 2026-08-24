# AmiAPI Roadmap

## M0 - Foundation — complete
Static library, opaque context, errors, version API, host build.

## M1 - TCP + HTTP — host implementation
- transport boundary
- POSIX backend
- HTTP/1.1 GET
- status and headers
- bounded body
- local integration test
- next: qualify `bsdsocket.library` backend on AmigaOS

## M2 - HTTPS
- AmiSSL transport
- certificate verification
- TLS error mapping

## M3 - Streaming HTTP
- response callbacks
- download-to-file
- redirects
- chunked transfer decoding
- cancellation and explicit limits

## M4 - JSON
- small-footprint parser
- event/streaming-oriented use
- UTF-8 policy

## M5 - Open-Meteo
- URL/query encoding
- real HTTPS + JSON example

## M6 - SDK preview
- API cleanup and docs
- AmigaOS cross-build qualification
- Aminet-style packaging

## M7 - SampleScout foundation
- Freesound provider
- search/metadata
- preview/download
- approved authentication model
