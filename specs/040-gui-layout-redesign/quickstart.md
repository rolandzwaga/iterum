# Quickstart: GUI Layout Redesign Implementation

**Feature**: 040-gui-layout-redesign
**Date**: 2025-12-30

## Overview

Reorganize all 11 mode panels in `resources/editor.uidesc` to group controls by functionality. Each group has a header label and distinct background.

## Key Files

| File | Action |
|------|--------|
| `resources/editor.uidesc` | Modify - reorganize all mode panels |
| `resources/editor_minimal.uidesc` | Review - may need similar updates |

## Implementation Pattern

### Step 1: Create Group Container

Wrap related controls in a `CViewContainer`:

```xml
<!-- Before: Controls scattered across panel -->
<view class="CSlider" origin="10, 50" ... />
<view class="CTextLabel" origin="10, 35" ... />

<!-- After: Controls grouped -->
<view class="CViewContainer" origin="10, 30" size="280, 175" background-color="section" transparent="false">
    <view class="CTextLabel" origin="5, 5" size="270, 18" title="TIME & MIX" font="section-font" font-color="accent" transparent="true"/>
    <view class="CTextLabel" origin="10, 28" ... />
    <view class="CSlider" origin="10, 48" ... />
</view>
```

### Step 2: Adjust Control Positions

Control positions become **relative to the group container**:

| Original Panel Position | Group Position | Control Position in Group |
|------------------------|----------------|---------------------------|
| (10, 50) | (10, 30) | (0, 20) |
| (10, 80) | (10, 30) | (0, 50) |
| (100, 50) | (10, 30) | (90, 20) |

**Formula**: `control_in_group = original - group_origin`

### Step 3: Add Group Header

Every group needs a header label:

```xml
<view class="CTextLabel"
      origin="5, 5"
      size="270, 18"
      title="GROUP NAME"
      font="section-font"
      font-color="accent"
      transparent="true"/>
```

## Layout Grid

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│ 0,0                                                               860,0         │
│   ┌───────────────────────────────────────────────────────────────────────────┐ │
│   │ MODE TITLE (10, 5)                                                        │ │
│   └───────────────────────────────────────────────────────────────────────────┘ │
│   ┌──────────────────┐ ┌──────────────────┐ ┌──────────────────┐               │
│   │ GROUP 1          │ │ GROUP 2          │ │ GROUP 4          │               │
│   │ TIME & MIX       │ │ [MODE-SPECIFIC]  │ │ [MODE-SPECIFIC]  │               │
│   │ (10, 30)         │ │ (300, 30)        │ │ (580, 30)        │               │
│   │ 280 x 175        │ │ 270 x 115        │ │ 270 x 120        │               │
│   │                  │ ├──────────────────┤ ├──────────────────┤               │
│   │                  │ │ GROUP 3          │ │ OUTPUT           │               │
│   │                  │ │ [MODE-SPECIFIC]  │ │ (580, 160)       │               │
│   │                  │ │ (300, 155)       │ │ 270 x 60         │               │
│   │                  │ │ 270 x 115        │ │                  │               │
│   └──────────────────┘ └──────────────────┘ └──────────────────┘               │
│                                                               860,400          │
└─────────────────────────────────────────────────────────────────────────────────┘
```

## Group Order by Mode

| Mode | Group 1 | Group 2 | Group 3 | Group 4 | Group 5 |
|------|---------|---------|---------|---------|---------|
| Granular | TIME & MIX | GRAIN PARAMETERS | SPRAY & RANDOMIZATION | GRAIN OPTIONS | OUTPUT |
| Spectral | TIME & MIX | SPECTRAL ANALYSIS | SPECTRAL CHARACTER | OUTPUT | — |
| Shimmer | TIME & MIX | PITCH SHIFT | DIFFUSION & FILTER | OUTPUT | — |
| Tape | TIME & MIX | CHARACTER | SPLICE | TAPE HEADS | OUTPUT |
| BBD | TIME & MIX | CHARACTER | MODULATION | OUTPUT | — |
| Digital | TIME & SYNC | CHARACTER | MODULATION | STEREO | OUTPUT |
| PingPong | TIME & SYNC | STEREO | MODULATION | OUTPUT | — |
| Reverse | TIME & MIX | CHUNK | FILTER | OUTPUT | — |
| MultiTap | TIME | PATTERN | MIX | FEEDBACK FILTERS | OUTPUT |
| Freeze | FREEZE CONTROL | TIME & MIX | PITCH & SHIMMER | DIFFUSION & FILTER | OUTPUT |
| Ducking | DUCKER DYNAMICS | DELAY | SIDECHAIN | OUTPUT | — |

## Existing Resources to Use

| Resource | Definition | Usage |
|----------|------------|-------|
| `section` color | #3a3a3a | Group background |
| `section-font` | Arial 14 bold | Group headers |
| `accent` color | #4a90d9 | Header text color |
| `panel` color | #353535 | Panel background |

## Validation Checklist

- [ ] All controls visible in each mode
- [ ] Group headers clearly readable
- [ ] Consistent spacing between groups (10px)
- [ ] All control tags still work (parameter binding)
- [ ] Plugin loads without errors
- [ ] Pluginval passes at strictness level 5

## Common Mistakes to Avoid

1. **Forgetting to adjust control positions** when moving into groups
2. **Missing group closing tag** `</view>`
3. **Wrong size calculation** - group must contain all child controls
4. **Not updating control-tag** references (should remain unchanged)
5. **Forgetting OUTPUT group** - should appear in every mode
