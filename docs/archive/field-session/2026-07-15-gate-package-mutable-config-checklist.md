# Gate package mutable config fix checklist

Process log: [Gate package mutable config fix log](2026-07-15-gate-package-mutable-config-log.md)

- [x] Confirm the operator contract: `default_config.csv` is mutable and requires no hash evidence.
- [x] Add regression coverage for immutable artifacts and required runtime files.
- [x] Keep only EXE/DLL in artifact hash evidence.
- [x] Require `bin/default_config.csv` to exist without hashing it.
- [x] Exclude mutable operator files from whole-package checksum verification.
- [x] Synchronize canonical, fixed-build, and copy-to-PC package files.
- [x] Update operator-facing HTML and supporting package documentation.
- [x] Validate scripts, manifests, checksums, package identity, and HTML.
- [x] Record remaining field-PC replacement step and archive this batch.

## Approved contract

- Immutable build artifacts: `bin/AnalogBoard_TestApp.exe`, `bin/AnalogBoard_Dll.dll`.
- Required mutable runtime file: `bin/default_config.csv`.
- The config file is checked for existence only. Its SHA-256 is neither validated nor written to Gate B/C inventory evidence.
- Writable checklist/result files and generated `evidence/` are outside immutable package checksum coverage.

## Remaining field step

Copy the refreshed `win11_driver_first_gate_package` to the measurement PC and run `run_00_verify_package.bat`. A real Gate B inventory PASS remains a field action and is not inferred from offline validation.
