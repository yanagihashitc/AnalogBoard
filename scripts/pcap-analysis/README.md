# Phase 0 USBPcap analyzer

This directory contains the provisional, payload-free Phase 0 analyzer for the six immutable captures under `artifacts/field-session/2026-07-17-characterization/`. It does not define the production Recorder format or the Frozen v1 output contract.

## Contract

- Streams TShark tab records; it never loads a complete capture into memory.
- Validates the exact Wireshark field inventory with `tshark -G fields` before accepting a run.
- Uses per-capture bus/device/descriptor/endpoint evidence; no device address is shared across captures.
- Correlates USBPcap IRPs using `usb.irp_id` and the observed `usb.irp_info.direction` request/completion marker. `usb.urb_type` is retained but is empty in the current USBPcap files.
- Keeps frame captured/reported length, USB data length, USBD status, truncation, cancellation, short transfer, duplicate, and unmatched categories distinct. NT status is recorded as unavailable because Wireshark 4.6.7 exposes no USBPcap NT-status field.
- Aggregates observed transfer bytes from `usb.data_len` on the direction-appropriate row: request for OUT endpoints (including EP2 `0x02`) and completion for IN endpoints (EP4 `0x84` and EP6 `0x86`). An OUT request with an IRP ID is finalized on completion or at capture end; reuse before completion replaces the stale request so superseded bytes are not counted twice. Every endpoint summary emits its `data_length_basis`; no payload bytes are exported.
- Computes `rate_bytes_per_second` from those selected lengths over the first-to-last observed endpoint-row window. It is a capture-window aggregate, not wire throughput or proven unique application payload. Duplicate and unmatched rows remain visible through correlation counters even when a superseded OUT request is excluded from byte totals.
- Retains both a known non-success EP4 completion after final EP6 and a later successful EP4 completion in frame order. The stop/drain observation reports the total known non-success count plus at most the first eight `{frame, status, usbd_status}` evidence entries, so a later success cannot turn the sequence into an unqualified clean result. Missing USBD status stays in the independent `unknown` status count/evidence and makes the tail observation incomplete; it is never classified as failure or success. Known raw statuses use payload-free lowercase `0x` plus eight-digit values such as `0xc0000011`.
- Requests no `usb.capdata` field. Generated JSON contains bounded counters and evidence frame numbers, not EP6 measurement bytes.
- Verifies source SHA-256 and size before and after each extraction.
- Accepts only integer source-manifest schema version `1`. Missing, non-integer, or unsupported versions fail before extraction.

The bounded-summary schema is version 2. Version 2 changes the existing endpoint `data_length` meaning for OUT traffic from completion-row-only to direction-appropriate `usb.data_len`, declares the basis explicitly, and adds bounded tail EP4 non-success evidence with both classified and raw USBD status. Consumers must check `schema_version` before interpreting these fields.

## Commands

Run from the repository root in WSL. The explicit executable paths are required because bare `tshark` and `capinfos` are not on PATH.

```bash
python3 scripts/pcap-analysis/analyze.py manifest \
  --source-root artifacts/field-session/2026-07-17-characterization \
  --output-root artifacts/field-session/2026-07-17-characterization/analysis \
  --tshark '/mnt/c/Program Files/Wireshark/tshark.exe' \
  --capinfos '/mnt/c/Program Files/Wireshark/capinfos.exe'

python3 scripts/pcap-analysis/analyze.py extract \
  --source-root artifacts/field-session/2026-07-17-characterization \
  --output-root artifacts/field-session/2026-07-17-characterization/analysis \
  --source-manifest artifacts/field-session/2026-07-17-characterization/analysis/source_manifest.json \
  --tshark '/mnt/c/Program Files/Wireshark/tshark.exe' \
  --captures low_mid.pcapng mid.pcapng low.pcapng high1.pcapng high2.pcapng idle_180_1700.pcapng
```

Generated output is regenerable and ignored under the capture-local `analysis/` directory. A source root itself, a source capture, or an arbitrary non-`analysis` child is rejected as an output location.

## Test execution

- Command: `python3 -m unittest discover -s scripts/pcap-analysis -p 'test_*.py' -v`
- Coverage target: all branches represented in the checklist test-perspective table. The environment does not have `coverage.py`, so no percentage is claimed; the focused suite explicitly covers missing/NULL/empty/malformed inputs, 0/min/-1 boundaries, external-tool failures, path safety, deterministic JSON, USBPcap request/completion correlation, device ambiguity, direction-appropriate transfer lengths, bounded tail evidence, zero/single/negative completion gaps, the 1/10/100/1000 ms gap edges, the 4 GiB tolerance edges, zero-duration rate handling, and bounded distributions.
