# FS-UAE / AmiSSL HTTPS test profile

## Status

Qualified on FS-UAE 3.2.35.

AmiSSL HTTPS was stress-tested successfully for 100 consecutive
connections using an emulated 68020 when FS-UAE was configured for
maximum CPU speed and non-cycle-exact emulation.

Result:

    100/100 successful

The AmiAPI `httpget` client itself was also stress-tested with 100
consecutive HTTPS requests to `https://www.example.com/`.

Result:

    100/100 successful

A final post-cleanup smoke test also returned HTTP 200 after the
informational TLS debug output was disabled.

## Qualified FS-UAE settings

The relevant `.fs-uae` settings are:

    amiga_model = A1200/020
    fpu = 68882
    bsdsocket_library = 1

    accuracy = -1
    uae_cpu_speed = max

FS-UAE generated the following effective UAE settings:

    cpu_speed=max
    cpu_type=68020/68881
    cpu_model=68020
    fpu_model=68882
    cpu_compatible=false
    cpu_cycle_exact=false
    blitter_cycle_exact=false

## Observed failure mode

The default A1200/020 configuration was not reliable under repeated
AmiSSL HTTPS connections.

Testing isolated the following:

- Standard A1200/020 configuration: unreliable.
- 68020 with maximum CPU speed alone: unreliable.
- 68020 with non-cycle-exact mode alone: unreliable.
- 68020 with maximum CPU speed AND non-cycle-exact mode: 100/100.
- Faster accelerator configurations were also observed to be reliable.

During investigation, the host networking layer returned a real EPIPE
after the remote peer had closed the TCP connection. The failure was
therefore not simply an incorrectly synthesized bsdsocket.library error.

## Interpretation

The results are consistent with a timing/performance problem during
CPU-intensive TLS processing under the standard FS-UAE A1200/020
emulation profile.

They do NOT establish that AmiSSL or AmiAPI requires a 68030 or 68040.

68020 remains the AmiAPI minimum CPU target.

## Development policy

For AmiAPI HTTPS testing under FS-UAE 3.2.35, use:

    accuracy = -1
    uae_cpu_speed = max

Do not treat failures reproduced only under real-speed/cycle-exact
FS-UAE emulation as evidence that AmiAPI itself requires a faster CPU
without additional testing on real hardware or another emulator.

