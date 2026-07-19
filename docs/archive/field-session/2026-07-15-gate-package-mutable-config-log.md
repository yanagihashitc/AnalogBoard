# Gate package mutable config fix Process Log

## References

- [Checklist](2026-07-15-gate-package-mutable-config-checklist.md)
- [Runbook](../../260706-field-session-runbook.html)

## Entries

| DateTime (JST) | Activity | Result | Evidence | Next |
|---|---|---|---|---|
| 2026-07-15 | Field package diagnosis | Reproduced by inspection | `default_config.csv` is exported on normal AnalogBoard close and was listed both as a hashed build artifact and in `checksums.sha256` | Confirm desired runtime contract |
| 2026-07-15 | Design approval | Approved | Owner selected existence-only validation; no config hash comparison or inventory evidence | Add Red regression coverage |
| 2026-07-15 | TDD Red | Expected failure | Fixture manifest declared two immutable artifacts plus one existence-only runtime file; missing-runtime assertion failed because the core did not enforce it | Implement required-runtime validation |
| 2026-07-15 | TDD Green / distribution | Pass | `Assert-GateRequiredRuntimeFiles` enforces mutable `existence_only` entries without `sha256`; collector version `GateInventory-v1.2`; canonical, fixed-build, and copy-to-PC collector/core hashes match | Refresh manifests and operator docs |
| 2026-07-15 | Package contract update | Pass | EXE/DLL remain immutable artifacts; config is a required runtime file; copy package excludes config, checklist, and result sheet from its 15-file checksum set | Run complete validation |
| 2026-07-15 12:57 | Validation / review | Pass | Contract tests PASS; PowerShell parse PASS; copy package verifier PASS for 15 immutable files plus config existence; fixed-build 19 checksums PASS; HTML parse, duplicate IDs, and local links PASS; config hash absent from copy package | Replace field-PC package and rerun `run_00` |
