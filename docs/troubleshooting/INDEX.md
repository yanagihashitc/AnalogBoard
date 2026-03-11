# Troubleshooting Index

Quick-reference index of all recorded troubleshooting knowledge.

## Categories

- [Build](build.md) - Compilation, linking, MSBuild
- [Environment](environment.md) - VS setup, toolchain, path, encoding, dependencies
- [USB](usb.md) - USB communication, device connection
- [Other](other.md) - Uncategorized runtime and tooling issues

## All Entries

| Date | Category | Title | File |
|------|----------|-------|------|
| 2026-03-06 | build | MSB4057 when invoking `AnalogBoard_UnitTest:Rebuild` on the solution | [build.md#msb4057-when-invoking-analogboard_unittestrebuild-on-the-solution](build.md#msb4057-when-invoking-analogboard_unittestrebuild-on-the-solution) |
| 2026-03-09 | build | Parallel `cl` invocations hit C1041 on shared `vc140.pdb` | [build.md#parallel-cl-invocations-hit-c1041-on-shared-vc140pdb](build.md#parallel-cl-invocations-hit-c1041-on-shared-vc140pdb) |
| 2026-03-09 | build | MSB4184 when `msbuild` cannot access `C:\Users\...\Microsoft SDKs` | [build.md#msb4184-when-msbuild-cannot-access-cusersmicrosoft-sdks](build.md#msb4184-when-msbuild-cannot-access-cusersmicrosoft-sdks) |
| 2026-03-11 | build | LNK4098 in `Debug|x64` because `CyAPI.lib` pulls `LIBCMT` | [build.md#lnk4098-in-debugx64-because-cyapilib-pulls-libcmt](build.md#lnk4098-in-debugx64-because-cyapilib-pulls-libcmt) |
| 2026-03-11 | build | C4996 / STL4017 when simulation code uses `<codecvt>` in VS2022 | [build.md#c4996--stl4017-when-simulation-code-uses-codecvt-in-vs2022](build.md#c4996--stl4017-when-simulation-code-uses-codecvt-in-vs2022) |
| 2026-03-06 | environment | Batch file output empty when invoked via bash cd + cmd | [environment.md#batch-file-output-empty-when-invoked-via-bash-cd--cmd](environment.md#batch-file-output-empty-when-invoked-via-bash-cd--cmd) |
| 2026-03-06 | environment | std::filesystem unavailable in MFC TestApp project (no C++17) | [environment.md#stdfilesystem-unavailable-in-mfc-testapp-project-no-c17](environment.md#stdfilesystem-unavailable-in-mfc-testapp-project-no-c17) |
| 2026-03-11 | usb | EP6 local scratch buffer regressed when `ScopedHeapBuffer` switched to `new[]/delete[]` | [usb.md#ep6-local-scratch-buffer-regressed-when-scopedheapbuffer-switched-to-newdelete](usb.md#ep6-local-scratch-buffer-regressed-when-scopedheapbuffer-switched-to-newdelete) |
| 2026-03-06 | other | TestApp APPCRASH at startup after FileLogger integration (MSVCP140.dll / 0xc0000005) | [other.md#testapp-appcrash-at-startup-after-filelogger-integration-msvcp140dll--0xc0000005](other.md#testapp-appcrash-at-startup-after-filelogger-integration-msvcp140dll--0xc0000005) |
| 2026-03-09 | other | New files under `docs/` do not appear in `git status` | [other.md#new-files-under-docs-do-not-appear-in-git-status](other.md#new-files-under-docs-do-not-appear-in-git-status) |
