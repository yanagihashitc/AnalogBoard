# Gate inventory single-device fix Process Log

## References

- [Checklist](2026-07-15-gate-inventory-single-device-fix-checklist.md)
- [Runbook](./260706-field-session-runbook.html)

## Entries

| DateTime (JST) | Activity | Result | Evidence | Next |
|---|---|---|---|---|
| 2026-07-15 12:00 | Field failure triage | Reproduced | One `PSCustomObject` returned from an `if` expression loses array shape; StrictMode raises `The property 'Count' cannot be found on this object.` | Add a formal Red regression test |
| 2026-07-15 12:01 | TDD Red | Expected failure | Contract test calls `ConvertTo-GateObjectArray` for a one-item fixture; missing function exits 1 | Implement array-preserving core helper |
| 2026-07-15 12:01 | TDD Green / distribution | Pass | Added `ConvertTo-GateObjectArray`; collector `GateInventory-v1.1` uses it around the complete PnP query; updated canonical, fixed-build package, and copy-to-PC package | Refresh checksum manifests and validate |
| 2026-07-15 12:02 | Validation | Pass | Contract tests pass; canonical/package PowerShell parsing pass; fixed package full checksum pass; copy-to-PC 18-file verification pass; collector/core copies match | Replace the field-PC package and rerun Gate B inventory |
