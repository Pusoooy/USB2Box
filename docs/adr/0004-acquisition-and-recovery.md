# ADR 0004 — ADC timing and bounded recovery

Status: accepted. Requirements A08, A11, A19, A20, I07, S13 and X20.

Timed ADC reads add an optional envelope and keep the legacy ADC body intact. The same integer timing planner runs in ARM and Host builds. Metadata reports a nominal device-time estimate from the start tick, programmed timer period and sequential rank conversion schedule. It carries no claim about measured trigger latency or sample accuracy.

I²C bus recovery is an explicit idle-master operation. It uses already-owned open-drain pins, can be cancelled and restores the configured peripheral. The normal command timeout retains its existing local controller reset path. Software never drives an I²C line high in recovery.

SPI cancellation snapshots completed DMA RX bytes before aborting. The diagnostic ring retains device events and errors within 16 entries. It stores no payloads.

No GPIO, DMA, or timer allocation changes. Release static RAM increased by 392 B relative to the preceding stream build. All changes work with the v1.0 header and an additive SYS feature mask.
