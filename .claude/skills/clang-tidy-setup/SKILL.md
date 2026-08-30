---
name: clang-tidy-setup
description: One-time clang-tidy environment setup (LLVM/Ninja install, compile_commands.json generation) and the full per-target invocation reference for run-clang-tidy.ps1/.sh. Use when clang-tidy is not yet set up on a machine, when the ninja preset needs regenerating, or when unsure of valid targets/flags.
---

# Clang-Tidy Setup & Invocation Reference

(Migrated from root CLAUDE.md — the canonical "run before every commit" rule stays there.)

## One-Time Setup (Windows)

1. Install LLVM (includes clang-tidy):
   ```powershell
   winget install LLVM.LLVM
   ```

2. Install Ninja (required for compile_commands.json):
   ```powershell
   winget install Ninja-build.Ninja
   ```

3. Generate compile_commands.json (run from VS Developer PowerShell):
   ```powershell
   # Open "Developer PowerShell for VS 2022" from Start Menu, then:
   cd F:\projects\iterum
   cmake --preset windows-ninja
   ```

**Re-run setup when:** CMakeLists.txt changes, new source files added, or compile flags change.

## Running Analysis

```powershell
# Analyze all plugin source files
./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja

# Analyze a specific target
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

# Apply automatic fixes (use with caution, review changes)
./tools/run-clang-tidy.ps1 -Target all -BuildDir build/windows-ninja -Fix
```

**Linux/macOS:**
```bash
cmake --preset linux-release   # or macos-release (generates compile_commands.json)
./tools/run-clang-tidy.sh --target all
./tools/run-clang-tidy.sh --target dsp --fix
```

Valid `--target` values (same roster on both scripts): `all`, `dsp`, `shared`, `iterum`,
`disrumpo`, `ruinae`, `innexus`, `gradus`, `membrum`, `seraphis`. **`all` MUST cover dsp +
every plugin** — if you add a plugin, add its case to BOTH `run-clang-tidy.ps1` and
`run-clang-tidy.sh` (and to their `all`), or the Linux/macOS pre-commit lint silently skips it.

## Configuration

The `.clang-tidy` file configures:
- Enabled: bugprone, performance, modernize, readability, concurrency, cppcoreguidelines
- Disabled: magic-numbers, short identifiers (DSP-friendly)
- Naming conventions matching the project style guide
