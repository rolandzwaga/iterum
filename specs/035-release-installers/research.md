# Research: Release Workflow with Platform-Specific Installers

**Date**: 2025-12-28
**Feature**: 035-release-installers

## Research Summary

This document consolidates research findings for implementing platform-specific VST3 plugin installers.

---

## 1. VST3 Installation Paths

### Decision: Use official Steinberg-documented paths

| Platform | Path | Type |
|----------|------|------|
| Windows | `C:\Program Files\Common Files\VST3\` | System-wide |
| macOS | `/Library/Audio/Plug-Ins/VST3/` | System-wide |
| Linux (user) | `~/.vst3/` | Per-user |
| Linux (system) | `/usr/lib/vst3/` | System-wide |

### Rationale

These paths are defined in the official Steinberg VST3 SDK documentation and are automatically scanned by compliant VST3 hosts (DAWs).

### Sources Consulted

- [Steinberg VST3 Developer Portal - Plugin Locations](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Locations.html)
- [Steinberg Help Center - Windows VST Locations](https://helpcenter.steinberg.de/hc/en-us/articles/115000177084-VST-plug-in-locations-on-Windows)
- [Steinberg Help Center - macOS VST Locations](https://helpcenter.steinberg.de/hc/en-us/articles/115000171310-VST-plug-in-locations-on-Mac-OS-X-and-macOS)

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| User-specific Windows path (`%APPDATA%`) | Not consistently scanned by all DAWs |
| User-specific macOS path (`~/Library/Audio/Plug-Ins/VST3/`) | Requires users to manually configure some DAWs |
| Custom installation path | Reduces discoverability; requires user configuration |

---

## 2. Windows Installer (Inno Setup)

### Decision: Use Inno Setup via GitHub Action

**Tool**: Inno Setup 6.x via `Minionguyjpro/Inno-Setup-Action@v1.2.2`

### Rationale

- Inno Setup is free, open-source, and widely used for Windows installers
- GitHub Action handles setup and compilation automatically
- Produces standard Windows installer behavior (silent install, uninstaller, etc.)
- Well-documented for audio plugin installation

### Key Script Settings

```inno
[Setup]
AppName=Iterum
AppVersion={#Version}
AppPublisher=Krate Audio
AppPublisherURL=https://github.com/rolandzwaga/iterum
DefaultDirName={commonpf}\Common Files\VST3
PrivilegesRequired=admin
OutputBaseFilename=Iterum-{#Version}-Windows-x64
Compression=lzma2
SolidCompression=yes

[Files]
Source: "Iterum.vst3\*"; DestDir: "{app}\Iterum.vst3"; Flags: recursesubdirs

[Icons]
Name: "{group}\Uninstall Iterum"; Filename: "{uninstallexe}"
```

### Sources Consulted

- [Inno Setup Documentation](https://jrsoftware.org/ishelp/)
- [HISE Forum - Inno Setup Script for VST](https://forum.hise.audio/topic/7832/automatic-installer-for-windows-inno-setup-script)
- [KVR Audio Forum - Inno Setup Examples](https://www.kvraudio.com/forum/viewtopic.php?t=543091)

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| WiX Toolset | More complex XML syntax, steeper learning curve |
| NSIS | Less intuitive script language than Inno Setup |
| MSIX/AppX | Requires Microsoft Store registration for full functionality |

---

## 3. macOS Installer (pkgbuild)

### Decision: Use native pkgbuild + productbuild

**Tools**: `pkgbuild` (create component package), `productbuild` (optional: create distribution)

### Rationale

- Native macOS tools, no additional dependencies
- Produces standard .pkg installer
- Handles system-wide installation with proper permissions

### Key Commands

```bash
# Create component package
pkgbuild \
  --root ./Iterum.vst3 \
  --identifier com.krateaudio.iterum.vst3 \
  --version "${VERSION}" \
  --install-location "/Library/Audio/Plug-Ins/VST3/Iterum.vst3" \
  Iterum-${VERSION}-macOS.pkg
```

### Sources Consulted

- [Apple pkgbuild Man Page](https://www.unix.com/man-page/osx/1/pkgbuild/)
- [Moonbase.sh - macOS Installers with pkgbuild](https://moonbase.sh/articles/how-to-make-macos-installers-for-juce-projects-with-pkgbuild-and-productbuild/)
- [Code Vamping - Creating macOS Installers](https://codevamping.com/2019/04/creating-macos-installers/)

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| DMG with drag-to-install | Requires user to manually navigate to VST3 folder |
| productbuild (full) | More complex; component package is sufficient for single plugin |
| Third-party installer tools | Unnecessary dependency when native tools work |

### Code Signing Note

macOS code signing and notarization are **out of scope for v1**. Without signing:
- Users will see Gatekeeper warning
- Users must right-click → Open to bypass
- This is acceptable for open-source projects

---

## 4. Linux Archive

### Decision: tar.gz archive with README

**Format**: `.tar.gz` (gzip-compressed tarball)

### Rationale

- Standard Linux distribution format
- No dependencies required
- Users expect to manually install plugins
- Flexibility for user-specific or system-wide installation

### Archive Structure

```
Iterum-{version}-Linux-x64.tar.gz
├── Iterum.vst3/
│   └── Contents/
│       ├── x86_64-linux/
│       │   └── Iterum.so
│       └── Resources/
│           └── editor.uidesc
└── README.txt
```

### README Content

```text
Iterum VST3 Plugin - Installation Instructions

QUICK INSTALL (per-user):
  cp -r Iterum.vst3 ~/.vst3/

SYSTEM-WIDE INSTALL (requires sudo):
  sudo cp -r Iterum.vst3 /usr/lib/vst3/

VERIFY INSTALLATION:
  Open your DAW and rescan plugins. Iterum should appear in the VST3 list.

SUPPORTED DAWS:
  - Bitwig Studio
  - Ardour
  - REAPER
  - Carla
  - Any VST3-compatible host

For issues, visit: https://github.com/rolandzwaga/iterum
```

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| .deb package | Debian/Ubuntu-specific; excludes other distros |
| .rpm package | Red Hat/Fedora-specific; excludes other distros |
| AppImage | Overkill for a plugin that integrates into a host |
| Flatpak | Not appropriate for plugins that need host integration |

---

## 5. GitHub Actions Workflow Pattern

### Decision: Reusable CI with separate release workflow

**Pattern**: `workflow_call` from release.yml to ci.yml

### Rationale

- CI workflow already builds and tests all platforms
- Avoids duplicating build logic
- Release only proceeds if CI passes
- Clear separation of concerns

### Workflow Structure

```yaml
# ci.yml adds:
on:
  workflow_call:  # Allow being called by other workflows

# release.yml:
jobs:
  build:
    uses: ./.github/workflows/ci.yml  # Reuse CI

  release:
    needs: build
    # ... create installers and release
```

### Sources Consulted

- [GitHub Docs - Reusing Workflows](https://docs.github.com/en/actions/using-workflows/reusing-workflows)
- [GitHub Docs - Creating Releases](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)

### Alternatives Considered

| Alternative | Why Rejected |
|-------------|--------------|
| Duplicate build steps in release.yml | Violates DRY principle; maintenance burden |
| Single workflow with conditional steps | More complex; harder to debug |
| Manual release process | Error-prone; not reproducible |

---

## 6. Version Extraction

### Decision: Extract version from git tag

**Method**: `${{ github.ref_name }}` gives the tag name (e.g., `v1.0.0`)

### Processing

```yaml
- name: Get Version
  id: version
  run: |
    VERSION="${{ github.ref_name }}"
    VERSION="${VERSION#v}"  # Strip leading 'v'
    echo "version=$VERSION" >> $GITHUB_OUTPUT
```

### Rationale

- Single source of truth (git tag)
- No need to parse CMakeLists.txt
- Semantic versioning enforced by tag naming

---

## Summary of Decisions

| Decision | Choice | Key Reason |
|----------|--------|------------|
| Installation paths | Steinberg official | DAW compatibility |
| Windows installer | Inno Setup | Free, well-documented, GitHub Action available |
| macOS installer | pkgbuild | Native tool, no dependencies |
| Linux format | tar.gz + README | Standard, flexible, no dependencies |
| Workflow pattern | Reusable CI | DRY principle, clear separation |
| Version source | Git tag | Single source of truth |
| Code signing | Out of scope (v1) | Cost; acceptable for OSS |
