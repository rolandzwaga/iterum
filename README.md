# Iterum

**A feature-rich VST3/AU delay plugin with 11 distinct delay modes**

[![Website](https://img.shields.io/badge/website-iterum-blue)](https://rolandzwaga.github.io/iterum/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)]()

[**Website & Documentation**](https://rolandzwaga.github.io/iterum/)

---

## Overview

Iterum aims to be a professional-grade delay plugin offering 11 unique delay algorithms, from classic tape and analog emulations to modern granular and spectral processing. Built with the Steinberg VST3 SDK and VSTGUI, it delivers high-quality audio processing with a focus on real-time performance and creative flexibility.

## Features

- **11 Delay Modes** - From vintage to experimental
- **Click-Free Mode Switching** - 50ms equal-power crossfade between all modes
- **Tempo Sync** - Lock delay times to host BPM with musical note values
- **Cross-Platform** - Windows, macOS (Intel & Apple Silicon), Linux
- **Multiple Formats** - VST3 and Audio Unit (macOS)

## Delay Modes

| Mode | Description |
|------|-------------|
| **Granular** | Granular processing with pitch/time spray, freeze, and tempo sync |
| **Spectral** | FFT-based per-band delays with frequency spreading |
| **Shimmer** | Pitch-shifted feedback with diffusion for ethereal textures |
| **Tape** | Classic tape echo with wow, flutter, saturation, and multi-head |
| **BBD** | Bucket-brigade analog character (MN3005, MN3007, MN3205, SAD1024) |
| **Digital** | Clean or vintage digital with era modeling (Pristine, 80s, Lo-Fi) |
| **Ping-Pong** | Stereo alternating delays with cross-feedback and width control |
| **Reverse** | Grain-based reverse processing with multiple playback modes |
| **Multi-Tap** | Up to 16 taps with timing/spatial patterns and morphing |
| **Freeze** | Infinite sustain with shimmer and decay control |
| **Ducking** | Envelope-based delay reduction for clarity in the mix |

## Installation

### Pre-built Releases

Download the latest release from the [Releases](https://github.com/rolandzwaga/iterum/releases) page.

- **Windows**: Copy `Iterum.vst3` to `C:\Program Files\Common Files\VST3\`
- **macOS**: Copy `Iterum.vst3` to `/Library/Audio/Plug-Ins/VST3/` and `Iterum.component` to `/Library/Audio/Plug-Ins/Components/`
- **Linux**: Copy `Iterum.vst3` to `~/.vst3/` or `/usr/lib/vst3/`

### Building from Source

#### Prerequisites

- CMake 3.20+
- C++20 compiler (MSVC 2022, Clang 14+, GCC 11+)
- VST3 SDK (included as submodule)

#### Build Steps

```bash
# Clone with submodules
git clone --recursive https://github.com/rolandzwaga/iterum.git
cd iterum

# Configure
cmake --preset windows-x64-release   # Windows
cmake --preset macos-release         # macOS
cmake --preset linux-release         # Linux

# Build
cmake --build --preset windows-x64-release
cmake --build --preset macos-release
cmake --build --preset linux-release
```

The built plugin will be in `build/<preset>/VST3/Release/`.

## Documentation

Full documentation is available at [rolandzwaga.github.io/iterum](https://rolandzwaga.github.io/iterum/).

## Technical Highlights

- **Real-Time Safe** - No allocations in audio thread, lock-free parameter updates
- **Layered DSP Architecture** - 5-layer compositional design for maintainability
- **Equal-Power Crossfades** - Smooth transitions without volume dips
- **Comprehensive Testing** - 1500+ unit tests with approval testing

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Author

**Roland Zwaga** - [Krate Audio](https://github.com/rolandzwaga)

---

*Built with the [Steinberg VST3 SDK](https://steinbergmedia.github.io/vst3_dev_portal/) and [VSTGUI](https://steinbergmedia.github.io/vst3_doc/vstgui/html/)*
