# Research: Preset Browser

**Status**: Complete
**Date**: 2025-12-31

## Research Sources

Primary research was conducted before specification and captured in:
- `specs/preset-browser-plan.md` - Detailed technical research and architecture

## Key Findings Summary

### VST3 Preset Handling

**Decision**: Use file-based presets with PresetFile class

**Research:**
- VST3 provides two approaches: file-based (host manages) vs program lists (plugin exposes bank)
- PresetFile class handles .vstpreset format with component state + metadata
- Standard preset locations defined per platform

**Sources:**
- [VST3 Developer Portal - Presets & Program Lists](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Presets+Program+Lists/Index.html)
- VST3 SDK: `public.sdk/source/vst/vstpresetfile.h`

### VSTGUI Custom Views

**Decision**: Use CDataBrowser with custom IDataBrowserDelegate

**Research:**
- CDataBrowser provides cross-platform list/table view with selection and scrolling
- IDataBrowserDelegate (or DataBrowserDelegateAdapter) for data provision
- CNewFileSelector for cross-platform file dialogs
- No built-in tree view - use flat list with mode filtering instead

**Sources:**
- VSTGUI: `vstgui/lib/cdatabrowser.h`
- VSTGUI: `vstgui/lib/idatabrowserdelegate.h`
- VSTGUI: `vstgui/lib/cfileselector.h`
- SDK example: `public.sdk/samples/vst/pitchnames/`

### Cross-Platform Compatibility

**Decision**: VSTGUI only, std::filesystem for paths, Platform:: namespace for isolation

**Research:**
- VSTGUI is inherently cross-platform when used correctly
- File paths differ per platform - isolate in Platform:: namespace
- CNewFileSelector wraps native dialogs automatically
- Rules documented in VST-GUIDE.md Section 8

**Sources:**
- `specs/VST-GUIDE.md` Section 8
- Constitution Principle VI

### Preset Organization

**Decision**: Mode-based folders with category subfolders, metadata in XML chunk

**Research:**
- Folder structure: `[Root]/[Mode]/[Category]/[Name].vstpreset`
- Metadata (name, category, mode) stored in preset's XML chunk
- Factory presets read-only, user presets writable
- Mode filtering via vertical tab bar

## Alternatives Rejected

| Alternative | Why Rejected |
|-------------|--------------|
| Custom binary format | No host integration, harder to debug |
| JSON metadata files | Separate from preset, sync issues |
| Program lists (IUnitInfo) | More complex, overkill for this use case |
| Native file dialogs | Violates cross-platform principle |
| Tree view for categories | VSTGUI doesn't have built-in tree, flat list simpler |
| Dropdown for mode filter | 12 options hard to scan, tabs clearer |

## Open Questions Resolved

| Question | Resolution |
|----------|------------|
| Store all modes or current only? | All modes - consistent with project save |
| How to determine preset's mode? | Metadata XML chunk, fallback to folder |
| Factory preset location? | Platform-specific shared folders |
| Popup vs sidebar? | Popup overlay - minimal UI disruption |
