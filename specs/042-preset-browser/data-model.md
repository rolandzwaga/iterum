# Data Model: Preset Browser

## Entities

### PresetInfo

Metadata for a single preset file.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| name | string | Yes | Display name (derived from filename without extension) |
| category | string | Yes | Category label (e.g., "Ambient", "Rhythmic") |
| mode | DelayMode | Yes | Target delay mode (0-10 enum value) |
| path | filesystem::path | Yes | Full path to .vstpreset file |
| isFactory | bool | Yes | True if factory preset (read-only) |
| description | string | No | Optional description text |
| author | string | No | Optional author name |

**Validation Rules:**
- name: Non-empty, no filesystem-invalid characters (`/ \ : * ? " < > |`)
- category: Non-empty string
- mode: Valid DelayMode enum value (0-10)
- path: Must exist and be readable for load, parent must be writable for save

### PresetCategory

Predefined category labels for organization.

| Category | Description |
|----------|-------------|
| Ambient | Atmospheric, pad-like delays |
| Rhythmic | Tempo-synced, pattern-based |
| Classic | Traditional delay sounds |
| Experimental | Unusual, creative effects |
| Vocal | Optimized for voice processing |
| Instrument | Tailored for specific instruments |
| Cinematic | Film/game soundtrack styles |
| Custom | User-defined categories |

### ModeFilter

Filter options for the mode tab bar.

| Value | Label | Filter Behavior |
|-------|-------|-----------------|
| -1 | All | Show all presets, add Mode column |
| 0 | Granular | Show only Granular presets |
| 1 | Spectral | Show only Spectral presets |
| 2 | Shimmer | Show only Shimmer presets |
| 3 | Tape | Show only Tape presets |
| 4 | BBD | Show only BBD presets |
| 5 | Digital | Show only Digital presets |
| 6 | PingPong | Show only PingPong presets |
| 7 | Reverse | Show only Reverse presets |
| 8 | MultiTap | Show only MultiTap presets |
| 9 | Freeze | Show only Freeze presets |
| 10 | Ducking | Show only Ducking presets |

## Relationships

```
PresetInfo
    ├── belongs to → Mode (1:1)
    ├── has → Category (1:1)
    └── stored at → Path (1:1)

PresetManager
    ├── scans → User Directory (1:1)
    ├── scans → Factory Directory (1:1)
    └── manages → PresetInfo collection (1:N)

PresetBrowserView
    ├── contains → ModeTabBar (1:1)
    ├── contains → CDataBrowser (1:1)
    ├── uses → PresetDataSource (1:1)
    └── references → PresetManager (1:1)
```

## State Transitions

### Preset Lifecycle

```
[File on Disk] --scan--> [PresetInfo in memory]
                              |
                         [Selected in list]
                              |
                    +---------+---------+
                    |                   |
               [Loaded]            [Deleted]
                    |                   |
        [Parameters restored]    [File removed]
                    |
               [Modified by user]
                    |
        +-----------+-----------+
        |                       |
   [Save (overwrite)]    [Save As (new)]
        |                       |
   [File updated]        [New file created]
```

### Browser State

```
[Closed] --open--> [Open]
                      |
              [Mode tab selected]
                      |
              [List filtered]
                      |
              [Preset selected]
                      |
         +------------+------------+
         |            |            |
    [Double-click] [Save As]   [Delete]
         |            |            |
    [Load preset]  [Dialog]   [Confirm]
         |            |            |
      [Close]     [Save]      [Remove]
                    |
                 [Refresh list]
```

## File Format

### .vstpreset Structure

Uses VST3 SDK PresetFile format:

```
[Header]
  - Magic: "VST3"
  - Version: int32
  - Class ID: 32 bytes (ASCII-encoded GUID)
  - Chunk list offset: int64

[Component State Chunk]
  - All 11 modes' parameters
  - Same format as Processor::getState()

[Controller State Chunk]
  - UI-only state (currently unused)

[Meta Info Chunk] (XML)
  - Preset name
  - Category
  - Target mode
  - Description
  - Author

[Chunk List]
  - Index of all chunks with offsets
```

### Metadata XML Schema

```xml
<?xml version="1.0" encoding="UTF-8"?>
<MetaInfo>
  <Attr id="PlugInName" value="Iterum"/>
  <Attr id="Name" value="Dreamy Echoes"/>
  <Attr id="Category" value="Ambient"/>
  <Attr id="TargetMode" value="5"/>
  <Attr id="Description" value="Lush, atmospheric delay"/>
  <Attr id="Author" value="Iterum"/>
</MetaInfo>
```

## Directory Structure

### User Presets (Writable)

```
Windows:  %USERPROFILE%\Documents\VST3 Presets\Iterum\Iterum\
macOS:    ~/Library/Audio/Presets/Iterum/Iterum/
Linux:    ~/.vst3/presets/Iterum/Iterum/

[User Preset Root]/
├── Granular/
│   ├── Ambient/
│   │   └── Dreamy Clouds.vstpreset
│   └── Rhythmic/
│       └── Stutter Gate.vstpreset
├── Spectral/
│   └── ...
└── [Mode]/
    └── [Category]/
        └── [Name].vstpreset
```

### Factory Presets (Read-Only)

```
Windows:  %PROGRAMDATA%\VST3 Presets\Iterum\Iterum\
macOS:    /Library/Audio/Presets/Iterum/Iterum/
Linux:    /usr/share/vst3/presets/Iterum/Iterum/

Same structure as user presets.
```

## Constraints

1. **Unique Names per Mode**: Preset names must be unique within a mode folder
2. **Valid Filenames**: Names must not contain: `/ \ : * ? " < > |`
3. **Max Name Length**: 255 characters (filesystem limit)
4. **Factory Protection**: Factory presets cannot be modified or deleted
5. **Mode Consistency**: Preset mode metadata should match folder location (fallback to metadata if mismatch)
