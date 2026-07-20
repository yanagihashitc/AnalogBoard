# Phase 0 USBPcap analyzer

This directory contains the provisional, payload-free Phase 0 analyzer for the six immutable captures under `artifacts/field-session/2026-07-17-characterization/`. It does not define the production Recorder format or the Frozen v1 output contract.

## Contract

- Streams TShark tab records; it never loads a complete capture into memory.
- Validates the exact Wireshark field inventory with `tshark -G fields` before accepting a run.
- Uses per-capture bus/device/descriptor/endpoint evidence; no device address is shared across captures.
- Correlates USBPcap IRPs using `usb.irp_id` and the observed `usb.irp_info.direction` request/completion marker. `usb.urb_type` is retained but is empty in the current USBPcap files.
- Keeps frame captured/reported length, USB data length, USBD status, truncation, cancellation, short transfer, duplicate, and unmatched categories distinct. NT status is recorded as unavailable because Wireshark 4.6.7 exposes no USBPcap NT-status field.
- Requests no `usb.capdata` field. Generated JSON contains bounded counters and evidence frame numbers, not EP6 measurement bytes.
- Verifies source SHA-256 and size before and after each extraction.

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
- Coverage target: all branches represented in the checklist test-perspective table. The environment does not have `coverage.py`, so no percentage is claimed; the focused suite explicitly covers missing/NULL/empty/malformed inputs, 0/min/-1 boundaries, external-tool failures, path safety, deterministic JSON, USBPcap request/completion correlation, device ambiguity, zero-duration rate handling, and bounded distributions.
