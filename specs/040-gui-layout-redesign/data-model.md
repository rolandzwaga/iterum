# Data Model: GUI Layout Group Structure

**Feature**: 040-gui-layout-redesign
**Date**: 2025-12-30

## Entity Definitions

### Control Group

A visual container holding related controls with a labeled header.

| Field | Type | Description |
|-------|------|-------------|
| name | string | Display name shown in header (uppercase) |
| id | string | Internal identifier for the group |
| position | {x, y} | Top-left origin within parent panel |
| size | {width, height} | Group container dimensions |
| backgroundColor | color | Background color reference |
| controls | Control[] | List of controls in this group |

### Mode Panel

Container for all groups in a specific delay mode.

| Field | Type | Description |
|-------|------|-------------|
| modeName | string | Mode identifier (Granular, Tape, etc.) |
| panelTemplate | string | Template name in editor.uidesc |
| size | {860, 400} | Fixed panel dimensions |
| groups | ControlGroup[] | Ordered list of groups |

## Group Definitions by Mode

### 1. Granular Mode (GranularPanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | TIME & MIX | Delay Time, Time Mode, Note Value, Feedback, Dry/Wet | (10, 30) | (280, 175) |
| 2 | GRAIN PARAMETERS | Grain Size, Density, Grain Pitch | (300, 30) | (270, 120) |
| 3 | SPRAY & RANDOMIZATION | Pitch Spray, Position Spray, Pan Spray, Reverse % | (300, 160) | (270, 115) |
| 4 | GRAIN OPTIONS | Envelope, Freeze, Jitter | (580, 30) | (270, 120) |
| 5 | OUTPUT | Output Level | (580, 160) | (270, 60) |

### 2. Spectral Mode (SpectralPanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | TIME & MIX | Delay Time, Feedback, Dry/Wet | (10, 30) | (280, 145) |
| 2 | SPECTRAL ANALYSIS | FFT Size, Spread, Direction | (300, 30) | (270, 145) |
| 3 | SPECTRAL CHARACTER | FB Tilt, Diffusion, Freeze | (580, 30) | (270, 145) |
| 4 | OUTPUT | Output Level | (10, 185) | (280, 60) |

### 3. Shimmer Mode (ShimmerPanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | TIME & MIX | Delay Time, Time Mode, Note Value, Feedback, Dry/Wet | (10, 30) | (280, 175) |
| 2 | PITCH SHIFT | Semitones, Cents, Shimmer Mix | (300, 30) | (270, 115) |
| 3 | DIFFUSION & FILTER | Diffusion, Filter Enable, Type, Cutoff | (300, 155) | (270, 120) |
| 4 | OUTPUT | Output Level | (580, 30) | (270, 60) |

### 4. Tape Mode (TapePanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | TIME & MIX | Delay Time, Time Mode, Note Value, Feedback, Dry/Wet | (10, 30) | (280, 175) |
| 2 | CHARACTER | Motor Speed, Inertia, Wear, Saturation, Age | (300, 30) | (270, 140) |
| 3 | SPLICE | Splice Enable, Splice Intensity | (300, 180) | (270, 60) |
| 4 | TAPE HEADS | Head 1/2/3 Enable, Level, Pan | (580, 30) | (270, 180) |
| 5 | OUTPUT | Output Level | (580, 220) | (270, 60) |

### 5. BBD Mode (BBDPanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | TIME & MIX | Delay Time, Time Mode, Note Value, Feedback, Dry/Wet | (10, 30) | (280, 175) |
| 2 | CHARACTER | Age, Era | (300, 30) | (270, 85) |
| 3 | MODULATION | Mod Depth, Mod Rate | (300, 125) | (270, 85) |
| 4 | OUTPUT | Output Level | (580, 30) | (270, 60) |

### 6. Digital Mode (DigitalPanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | TIME & SYNC | Delay Time, Time Mode, Note Value, Feedback, Dry/Wet | (10, 30) | (280, 175) |
| 2 | CHARACTER | Era, Age, Limiter | (300, 30) | (270, 115) |
| 3 | MODULATION | Mod Depth, Mod Rate, Waveform | (300, 155) | (270, 115) |
| 4 | STEREO | Width | (580, 30) | (270, 60) |
| 5 | OUTPUT | Output Level | (580, 100) | (270, 60) |

### 7. PingPong Mode (PingPongPanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | TIME & SYNC | Delay Time, Time Mode, Note Value, Feedback, Dry/Wet | (10, 30) | (280, 175) |
| 2 | STEREO | L/R Ratio, Width, Cross FB | (300, 30) | (270, 115) |
| 3 | MODULATION | Mod Depth, Mod Rate | (300, 155) | (270, 85) |
| 4 | OUTPUT | Output Level | (580, 30) | (270, 60) |

### 8. Reverse Mode (ReversePanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | TIME & MIX | Delay Time, Feedback, Dry/Wet | (10, 30) | (280, 145) |
| 2 | CHUNK | Chunk Size, Crossfade, Reverse Mode | (300, 30) | (270, 115) |
| 3 | FILTER | Filter Enable, Type, Cutoff | (300, 155) | (270, 95) |
| 4 | OUTPUT | Output Level | (580, 30) | (270, 60) |

### 9. MultiTap Mode (MultiTapPanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | TIME | Base Time, Tempo, Time Mode, Note Value | (10, 30) | (280, 145) |
| 2 | PATTERN | Timing Pattern, Spatial Pattern, Tap Count | (300, 30) | (270, 115) |
| 3 | MIX | Feedback, Dry/Wet | (10, 185) | (280, 85) |
| 4 | FEEDBACK FILTERS | LP Cutoff, HP Cutoff | (300, 155) | (270, 85) |
| 5 | MORPHING | Morph Time | (580, 30) | (270, 60) |
| 6 | OUTPUT | Output Level | (580, 100) | (270, 60) |

### 10. Freeze Mode (FreezePanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | FREEZE CONTROL | Freeze Enable, Delay, Feedback, Decay | (10, 30) | (280, 145) |
| 2 | TIME & MIX | Delay Time, Time Mode, Note Value, Dry/Wet | (10, 185) | (280, 115) |
| 3 | PITCH & SHIMMER | Semitones, Cents, Shimmer Mix | (300, 30) | (270, 115) |
| 4 | DIFFUSION & FILTER | Diffusion, Filter Enable, Type, Cutoff | (300, 155) | (270, 120) |
| 5 | OUTPUT | Output Level | (580, 30) | (270, 60) |

### 11. Ducking Mode (DuckingPanel)

| Group | Name | Controls | Position | Size |
|-------|------|----------|----------|------|
| 1 | DUCKER DYNAMICS | Threshold, Duck Amount, Attack, Release, Hold, Target | (10, 30) | (280, 200) |
| 2 | DELAY | Delay Time, Time Mode, Note Value, Feedback, Dry/Wet | (300, 30) | (270, 175) |
| 3 | SIDECHAIN | SC Filter Enable, SC Cutoff | (300, 215) | (270, 60) |
| 4 | OUTPUT | Output Level | (580, 30) | (270, 60) |

## Color Scheme

| Color Name | Hex Value | Usage |
|------------|-----------|-------|
| panel | #353535 | Mode panel background |
| section | #3a3a3a | Group container background |
| text | #ffffff | Control labels |
| text-dim | #888888 | Secondary labels |
| accent | #4a90d9 | Group headers, highlights |

## Typography

| Font Name | Properties | Usage |
|-----------|------------|-------|
| section-font | Arial 14 bold | Group headers |
| label-font | Arial 11 | Control labels |
| value-font | Arial 10 | Parameter values |

## XML Structure Template

```xml
<template name="[Mode]Panel" class="CViewContainer" origin="0, 0" size="860, 400" background-color="panel" transparent="false">
    <!-- Mode Title -->
    <view class="CTextLabel" origin="10, 5" size="200, 22" title="[MODE NAME] DELAY" font="section-font" font-color="accent" transparent="true"/>

    <!-- Group 1: TIME & MIX -->
    <view class="CViewContainer" origin="10, 30" size="280, 175" background-color="section" transparent="false">
        <view class="CTextLabel" origin="5, 5" size="270, 18" title="TIME & MIX" font="section-font" font-color="accent" transparent="true"/>
        <!-- Controls here, positions relative to group -->
    </view>

    <!-- Group 2: [GROUP NAME] -->
    <view class="CViewContainer" origin="300, 30" size="270, 115" background-color="section" transparent="false">
        <view class="CTextLabel" origin="5, 5" size="260, 18" title="[GROUP NAME]" font="section-font" font-color="accent" transparent="true"/>
        <!-- Controls here -->
    </view>

    <!-- Additional groups... -->
</template>
```
