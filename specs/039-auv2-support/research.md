# Research: Audio Unit (AUv2) Support

**Branch**: `039-auv2-support` | **Date**: 2025-12-30 | **Spec**: [spec.md](spec.md)

## Executive Summary

The VST3 SDK provides a complete AU wrapper that translates VST3 plugins to Audio Unit v2 format. Implementation requires:
1. FetchContent for Apple's AudioUnitSDK
2. CMake configuration with `smtg_target_add_auv2()`
3. AU manifest file (au-info.plist)
4. CI/CD workflow modifications for build and validation

No C++ code changes required - the VST3 SDK's AU wrapper handles all translation.

## VST3 SDK AU Wrapper Analysis

### Source Location
```
extern/vst3sdk/public.sdk/source/vst/auwrapper/
├── auwrapper.mm/.h       # Main AU wrapper implementation
├── aucocoaview.mm/.h     # Cocoa UI wrapper
├── NSDataIBStream.mm/.h  # Data stream utilities
└── ausdk.mm              # AudioUnitSDK integration
```

### How It Works

1. **Plugin Embedding**: The AU wrapper embeds the VST3 bundle inside the AU component:
   ```
   Iterum.component/
   └── Contents/
       └── Resources/
           └── plugin.vst3 → symlink to Iterum.vst3
   ```

2. **Translation Layer**: `auwrapper.mm` implements:
   - `AUMIDIEffectBase` or `AUEffectBase` depending on plugin type
   - Parameter mapping (VST3 normalized → AU float)
   - Audio buffer translation
   - State serialization/deserialization
   - Preset management

3. **UI Hosting**: `aucocoaview.mm` creates a Cocoa view that hosts the VSTGUI editor.

### CMake Function: `smtg_target_add_auv2`

**Location**: `extern/vst3sdk/cmake/modules/SMTG_AddVST3AuV2.cmake`

**Signature**:
```cmake
smtg_target_add_auv2(<target>
    BUNDLE_NAME <name>
    BUNDLE_IDENTIFIER <id>
    INFO_PLIST_TEMPLATE <path>
    VST3_PLUGIN_TARGET <target>
)
```

**Parameters**:
| Parameter | Description | Example Value |
|-----------|-------------|---------------|
| target | AU target name (distinct from VST3) | `Iterum_AU` |
| BUNDLE_NAME | Display name for AU bundle | `Iterum` |
| BUNDLE_IDENTIFIER | Reverse-domain identifier | `com.krateaudio.iterum.audiounit` |
| INFO_PLIST_TEMPLATE | Path to au-info.plist | `${CMAKE_CURRENT_SOURCE_DIR}/resources/au-info.plist` |
| VST3_PLUGIN_TARGET | VST3 target to wrap | `Iterum` |

**Prerequisites**:
```cmake
if(SMTG_MAC AND XCODE AND SMTG_ENABLE_AUV2_BUILDS)
```

### Automatic Features

The `smtg_target_add_auv2` function automatically:
- Links required macOS frameworks (AudioUnit, CoreMIDI, AudioToolbox, etc.)
- Sets up Universal Binary via `smtg_target_setup_universal_binary()`
- Creates symlink from AU Resources to VST3 bundle
- Copies AU to `~/Library/Audio/Plug-Ins/Components/` post-build

## Apple AudioUnitSDK

### FetchContent Setup

Apple deprecated the CoreAudio SDK and now provides AudioUnitSDK separately:

```cmake
include(FetchContent)
FetchContent_Declare(
    AudioUnitSDK
    GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
    GIT_TAG AudioUnitSDK-1.1.0  # Use specific tag for reproducibility
)
FetchContent_MakeAvailable(AudioUnitSDK)
FetchContent_GetProperties(
    AudioUnitSDK
    SOURCE_DIR SMTG_AUDIOUNIT_SDK_PATH
)
```

**Key Variable**: `SMTG_AUDIOUNIT_SDK_PATH` - must be set before calling `smtg_target_add_auv2()`.

### Build Process

The CMake function builds AudioUnitSDK via xcodebuild:
```bash
xcodebuild -project AudioUnitSDK.xcodeproj -target AudioUnitSDK build -configuration Release
```

This produces `libAudioUnitSDK.a` which is linked into the AU wrapper.

## AU Manifest (au-info.plist)

### Structure

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>English</string>
    <key>CFBundleExecutable</key>
    <string>$(EXECUTABLE_NAME)</string>
    <key>CFBundleIdentifier</key>
    <string>$(PRODUCT_BUNDLE_IDENTIFIER)</string>
    <key>CFBundleName</key>
    <string>$(PRODUCT_NAME)</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
    <key>CFBundleSignature</key>
    <string>????</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CSResourcesFileMapped</key>
    <string>yes</string>
    <key>AudioComponents</key>
    <array>
        <dict>
            <key>factoryFunction</key>
            <string>AUWrapperFactory</string>
            <key>description</key>
            <string>Iterum Delay</string>
            <key>manufacturer</key>
            <string>KrAt</string>
            <key>name</key>
            <string>Krate Audio: Iterum</string>
            <key>subtype</key>
            <string>Itrm</string>
            <key>type</key>
            <string>aufx</string>
            <key>version</key>
            <integer>1</integer>
        </dict>
    </array>
    <key>AudioUnit SupportedNumChannels</key>
    <array>
        <dict>
            <key>Outputs</key>
            <string>2</string>
            <key>Inputs</key>
            <string>2</string>
        </dict>
    </array>
</dict>
</plist>
```

### AudioComponents Keys (FR-011, FR-012, FR-013)

| Key | Value | Description |
|-----|-------|-------------|
| type | `aufx` | Audio effect (not `aumu` instrument, `aumf` music effect) |
| manufacturer | `KrAt` | 4-char code for Krate Audio |
| subtype | `Itrm` | 4-char code for Iterum |
| factoryFunction | `AUWrapperFactory` | Entry point provided by VST3 SDK |
| name | `Krate Audio: Iterum` | Display name in AU host menus |

### Channel Configuration

The `AudioUnit SupportedNumChannels` array defines supported I/O configurations:
- `2 in → 2 out` (stereo)

Matches existing VST3 plugin's stereo configuration.

## Universal Binary (FR-002)

### Configuration

`SMTG_UniversalBinary.cmake` automatically configures:
- Xcode 12+: `x86_64;arm64;arm64e` (Intel + Apple Silicon)
- Debug builds: Active architecture only (faster builds)
- Release builds: All architectures (universal binary)

### Verification

```bash
# Check architectures in built binary
file Iterum.component/Contents/MacOS/Iterum
# Expected: Mach-O universal binary with 2 architectures: [x86_64:bundle x86_64] [arm64:bundle arm64]

# Or use lipo
lipo -info Iterum.component/Contents/MacOS/Iterum
# Expected: Architectures in the fat file: x86_64 arm64
```

## AU Validation (FR-003)

### auval Command

```bash
auval -v aufx Itrm KrAt
```

**Parameters**:
- `-v`: Verbose output
- `aufx`: Component type (audio effect)
- `Itrm`: Component subtype
- `KrAt`: Manufacturer code

### Expected Output

```
AU Validation Tool - Version: x.x.x
...
VALIDATING AUDIO UNIT: Krate Audio: Iterum - aufx Itrm KrAt
...
---------------------------
TESTING AUDIO UNIT
---------------------------
* Tests Passed: XX
* Tests Failed: 0
...
AU VALIDATION SUCCEEDED
```

### Common Failure Points

1. **Parameter ranges**: AU expects 0.0-1.0 normalized - VST3 SDK handles this
2. **State restoration**: Must restore identical state - handled by wrapper
3. **Silence processing**: Must handle silence correctly - verify with silence input test
4. **Latency reporting**: Must match VST3 latency - handled by wrapper

### CI Validation Step

```yaml
- name: Run AU Validation
  run: |
    # Register the AU component
    cp -r build/VST3/Release/Iterum.component ~/Library/Audio/Plug-Ins/Components/
    # Force AU cache refresh
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    # Run validation
    auval -v aufx Itrm KrAt
```

## CI/CD Integration

### ci.yml Modifications

```yaml
build-macos:
  steps:
    - name: Configure CMake
      run: cmake -S . -B build -G Xcode -DUSE_CCACHE=ON -DSMTG_ENABLE_AUV2_BUILDS=ON

    - name: Build Plugin (VST3 + AU)
      run: cmake --build build --config Release --target Iterum Iterum_AU --parallel

    - name: Run AU Validation
      run: |
        cp -r build/VST3/Release/Iterum.component ~/Library/Audio/Plug-Ins/Components/
        auval -v aufx Itrm KrAt

    - name: Upload Plugin Artifacts
      uses: actions/upload-artifact@v4
      with:
        name: Iterum-macOS
        path: |
          build/VST3/Release/Iterum.vst3
          build/VST3/Release/Iterum.component
```

### release.yml Modifications

```yaml
macos-installer:
  steps:
    - name: Prepare Installer Files
      run: |
        mkdir -p staging/Library/Audio/Plug-Ins/VST3
        mkdir -p staging/Library/Audio/Plug-Ins/Components
        mv artifact/Iterum.vst3 staging/Library/Audio/Plug-Ins/VST3/
        mv artifact/Iterum.component staging/Library/Audio/Plug-Ins/Components/

    - name: Build Installer
      run: |
        pkgbuild \
          --root staging \
          --identifier com.krateaudio.iterum \
          --version ${{ needs.version.outputs.version }} \
          --install-location / \
          Iterum-${{ needs.version.outputs.version }}-macOS.pkg
```

## Implementation Checklist

1. **Create au-info.plist** in `resources/`
   - Set type=`aufx`, manufacturer=`KrAt`, subtype=`Itrm`
   - Configure stereo channel support

2. **Update CMakeLists.txt**
   - Add AudioUnitSDK FetchContent
   - Add SMTG_ENABLE_AUV2_BUILDS option
   - Call smtg_target_add_auv2() for macOS + Xcode

3. **Update .github/workflows/ci.yml**
   - Enable SMTG_ENABLE_AUV2_BUILDS=ON
   - Add Iterum_AU to build targets
   - Add auval validation step
   - Update artifact upload to include AU

4. **Update .github/workflows/release.yml**
   - Add Components directory to staging
   - Move AU component to staging
   - Update installer identifier

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| auval validation failures | Low | High | VST3 SDK wrapper is well-tested |
| Universal Binary issues | Low | Medium | Xcode handles automatically |
| CI FetchContent rate limits | Medium | Low | Use specific tag with SHA |
| State compatibility | Low | Medium | Same serialization as VST3 |

## References

- [VST3 SDK AU Tutorial](https://steinbergmedia.github.io/vst3_dev_portal/pages/Tutorials/Audio+Unit+wrapping.html)
- [Apple AudioUnitSDK](https://github.com/apple/AudioUnitSDK)
- [auval documentation](https://developer.apple.com/documentation/audiotoolbox/audio_unit_hosting_guide_for_ios)
- [Audio Unit Programming Guide](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/)
