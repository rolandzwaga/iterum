# Quickstart: UI Control Refactoring

## Overview

This feature converts 21 COptionMenu dropdown controls to CSegmentButton controls in `editor.uidesc`.

## Prerequisites

- Build system configured (`cmake --preset windows-x64-release`)
- DAW for manual testing
- pluginval in `tools/` directory

## Implementation Pattern

For each control to convert:

```xml
<!-- BEFORE: COptionMenu -->
<view class="COptionMenu"
      control-tag="GranularTimeMode"
      ... />

<!-- AFTER: CSegmentButton -->
<view class="CSegmentButton"
      control-tag="GranularTimeMode"
      style="horizontal"
      segment-names="Free,Synced"
      selection-mode="kSingle"
      font="~ NormalFontSmall"
      text-color="~ WhiteCColor"
      frame-color="~ GreyCColor"
      ... />
```

## File to Modify

**Only one file**: `plugins/iterum/resources/editor.uidesc`

## Controls to Convert

| Phase | Controls | Segment Pattern |
|-------|----------|-----------------|
| 1 | 10 TimeMode controls | `Free,Synced` |
| 2 | 2 FilterType controls | `LP,HP,BP` |
| 3 | 2 Era controls | BBD: 4, Digital: 3 |
| 4 | 7 mode-specific | Various (2-5 segments) |

## Verification

After each phase:

```bash
# Build
cmake --build build --config Release

# Test in DAW (manual)
# 1. Load plugin
# 2. Check each modified panel
# 3. Verify segment selection works
# 4. Verify parameter changes

# Validate
tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Iterum.vst3"
```

## Segment Names Reference

See `specs/045-ui-control-refactor/spec.md` section "Controls by Mode Panel" for exact `segment-names` values for each control.
