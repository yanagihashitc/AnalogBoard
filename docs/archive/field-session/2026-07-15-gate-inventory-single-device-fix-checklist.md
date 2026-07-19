# Gate inventory single-device fix checklist

Process log: [Gate inventory single-device fix log](2026-07-15-gate-inventory-single-device-fix-log.md)

- [x] Reproduce the StrictMode `.Count` failure when one FX3 device is returned.
- [x] Add a failing regression test for a one-item candidate array.
- [x] Preserve zero, one, and multiple candidates as arrays.
- [x] Update canonical, fixed-build package, and copy-to-PC package collectors.
- [x] Refresh both package checksum manifests.
- [x] Validate PowerShell parsing, contract tests, package hashes, and copy identity.
- [x] Record the remaining live-hardware rerun and archive this batch.

## Remaining field step

Replace the package on the measurement PC and rerun `run_01_gate_b_inventory.bat` with AnalogBoard closed. The implementation batch does not fabricate a live FX3 PASS.
