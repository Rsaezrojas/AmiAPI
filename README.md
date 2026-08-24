# AmiAPI M1.1 fix / 0.1.2

Targeted crash fix for classic AmigaOS.

The key change is moving the 4096-byte receive buffer out of `AmiAPI_Get()`'s
stack frame and onto the heap. Classic AmigaDOS processes can have very small
stacks, so the previous local `unsigned char chunk[4096]` could overflow the
process stack before the network call and later surface as Exec memory-list
corruption/Gurus.

Also included: hardened dynamic-buffer accounting with an explicit NUL-byte
capacity invariant, cleaned diagnostic code, private bsdsocket base, and normal
response/context cleanup restored.

Chunked transfer decoding is not implemented yet; chunked bodies are returned
raw in this milestone.
