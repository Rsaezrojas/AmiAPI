# AmiAPI Architecture

## Baseline

AmiAPI targets AmigaOS 3.x on 68020+ with no FPU or MMU requirement. A stock
A1200 with Fast RAM is the reference performance class.

## Design

Applications use the public AmiAPI interface. Protocol code does not call
platform socket APIs directly; it goes through a small transport boundary.

    Application
        |
    AmiAPI public API
        |
    HTTP
        |
    transport boundary
       / \
  POSIX   AmigaOS (bsdsocket.library; next)
        |
       TCP

HTTPS will add a TLS transport without requiring HTTP users to understand TLS.

M1 intentionally buffers the response body but enforces a configurable upper
bound (1 MiB by default). M3 replaces this as the primary model with streaming
callbacks/download-to-file while retaining a convenient bounded-buffer API.

The static library remains the development format until the API has enough
real-world use to justify freezing a shared-library ABI.
