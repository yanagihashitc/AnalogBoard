---
name: msvc-build
description: Build and unit-test the legacy AnalogBoard MSVC (Windows C++) targets from this WSL environment. Use for legacy C++ build, rebuild, clean, compile, unit-test, or coverage requests involving AnalogBoard_TestApp.sln, .vcxproj, msbuild, or cl.exe. The build runs on the Windows MSVC toolchain (VS2022) bridged from WSL via cmd.exe plus scripts\run_with_vsdevcmd.bat. Do NOT use for the P0-R1 .NET 10 WPF standalone prototype or its scripts/scatter-rendering/verify.ps1 wrapper; this skill covers legacy C++ only.
---

# MSVC Build (AnalogBoard, from WSL)

## Scope boundary

This skill is for the legacy C++ `AnalogBoard_TestApp.sln` surface only. Do not use
it for P0-R1, which targets `net10.0-windows` with .NET SDK `10.0.302` and Desktop
Runtime `10.0.10`; run P0-R1 verification through
`scripts/scatter-rendering/verify.ps1` as pinned by `goal.md`.

This repo lives in WSL but builds its legacy C++ surface against the **Windows**
MSVC toolchain (VS 2022 Community, MSVC 19.3x, x64). Every legacy C++ Windows
command must go through
`scripts\run_with_vsdevcmd.bat`, which initializes VsDevCmd for x64. Invoking that
from bash requires bridging `cmd.exe` and getting multiple layers of quoting right.

**Always use the helper — do not hand-write the `cmd.exe /d /c "..."` invocation.**

## Primary path: `scripts/build.sh`

Located next to this file. It derives the repo root (git toplevel) and its Windows
path (`wslpath -w`) automatically, so it works from any cwd. Run it directly:

```bash
.claude/skills/msvc-build/scripts/build.sh <command> [args]
```

| Command | Effect |
|---|---|
| `app [Debug\|Release]`        | Build the solution (default Debug). |
| `rebuild [Debug\|Release]`    | Clean + build the solution. |
| `clean [Debug\|Release]`      | Clean solution outputs. |
| `proj NAME [Debug\|Release]`  | Build one project target: `AnalogBoard_TestApp` or `AnalogBoard_Dll`. |
| `test`                        | Build **and run** the unit-test suites (see below). |
| `coverage`                    | OpenCppCoverage gate, 80% line threshold (needs `OpenCppCoverage` on PATH). |
| `raw -- <args...>`            | Pass args straight to the VsDevCmd wrapper (arbitrary `msbuild` / `cl`). |

The helper echoes `>> (win) <the exact command>` to stderr before running, so the
underlying invocation stays visible. It exits non-zero on build/test failure.

## Build surface (current repo)

- **Solution**: `AnalogBoard_TestApp.sln` — projects `AnalogBoard_TestApp` (test app)
  and `AnalogBoard_Dll`. Configs: `Debug|x64`, `Release|x64` (x64 only).
- **Unit tests**: `AnalogBoard_UnitTest\build_test.bat` — compiles ~10 policy-logic
  suites with `cl` and runs each `.exe`. **Not an msbuild target and NOT in the .sln.**
  Use `build.sh test`.
- **Coverage**: `scripts\run_coverage.bat` — builds tests + OpenCppCoverage, fails under 80%.

### ⚠ CLAUDE.md discrepancy

The CLAUDE.md "Windows Build/Test Execution" example shows
`msbuild ... /t:AnalogBoard_UnitTest:Rebuild`. **That target does not exist** — the
sln contains no `AnalogBoard_UnitTest` project; unit tests build via `build_test.bat`.
For a unit-test run use `build.sh test`, not that msbuild line. Report the mismatch
if it becomes load-bearing rather than silently reconciling it.

## Prerequisites (already verified present)

- WSL interop enabled (`cmd.exe` reachable); binfmt `WSLInterop` registered.
- VS 2022 Community with the `VC.Tools.x86.x64` component; the wrapper auto-detects it.
- Repo must resolve to a real Windows drive path (`wslpath -w` succeeds). Both mounted
  working dirs here map to `D:\...`, so this holds from either.

## Gotchas

- **Never nest `cmd /c "..."` inside the wrapper call from bash** — the quotes get
  mangled (`'"msbuild ...'` is not recognized). Pass args as a flat list; the wrapper
  already does its own `cmd /d /c "%*"`. `build.sh` handles this correctly.
- Running from a `\\wsl$` (Linux-home) cwd makes cmd.exe print a harmless
  "UNC パスはサポートされません" warning before the `cd /d` takes effect; the build
  still succeeds. `build.sh` always `cd /d`s into the Windows repo path first.
- Keep `/m` (parallel) but pin `/m:1` if a data race in a custom build step is suspected.
- Build-tooling changes must not touch runtime code. Do not add logging/instrumentation
  to the acquisition hot path to "debug a build" — see `acquisition-hotpath-guard`.

## Manual invocation (fallback, if the helper is unavailable)

```bash
cmd.exe /d /c "cd /d $(wslpath -w "$(git rev-parse --show-toplevel)") && \
  scripts\\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln \
  /p:Configuration=Debug /p:Platform=x64 /m /nologo /v:minimal"
```
