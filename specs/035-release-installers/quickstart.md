# Quickstart: Creating a Release

This guide explains how to create a new release of the Iterum VST3 plugin with platform-specific installers.

## Prerequisites

- All changes merged to `main` branch
- All CI tests passing on main
- Version number decided (semantic versioning: `MAJOR.MINOR.PATCH`)

## Step 1: Update Version in CMakeLists.txt

Edit `CMakeLists.txt` and update the project version:

```cmake
project(Iterum
    LANGUAGES CXX
    VERSION 1.0.0  # <-- Update this
)
```

Commit the version change:

```bash
git add CMakeLists.txt
git commit -m "chore: bump version to 1.0.0"
git push origin main
```

## Step 2: Create and Push a Version Tag

Create a tag matching the version with a `v` prefix:

```bash
git tag v1.0.0
git push origin v1.0.0
```

## Step 3: Wait for Release Workflow

The release workflow will automatically:

1. Run the full CI build and test suite on all platforms
2. If CI passes, download the build artifacts
3. Create platform-specific installers:
   - Windows: `Iterum-1.0.0-Windows-x64.exe`
   - macOS: `Iterum-1.0.0-macOS.pkg`
   - Linux: `Iterum-1.0.0-Linux-x64.tar.gz`
4. Create a GitHub Release with all installers attached

Monitor progress at: `https://github.com/rolandzwaga/iterum/actions`

## Step 4: Verify the Release

Once the workflow completes:

1. Go to the [Releases page](https://github.com/rolandzwaga/iterum/releases)
2. Verify the new release exists with tag `v1.0.0`
3. Download and test each installer on its target platform

### Windows Verification

1. Download `Iterum-1.0.0-Windows-x64.exe`
2. Run the installer
3. Verify plugin appears at `C:\Program Files\Common Files\VST3\Iterum.vst3`
4. Open a DAW and verify Iterum appears in the plugin list

### macOS Verification

1. Download `Iterum-1.0.0-macOS.pkg`
2. Double-click to install (may need to right-click → Open due to unsigned pkg)
3. Verify plugin appears at `/Library/Audio/Plug-Ins/VST3/Iterum.vst3`
4. Open a DAW and verify Iterum appears in the plugin list

### Linux Verification

1. Download `Iterum-1.0.0-Linux-x64.tar.gz`
2. Extract: `tar -xzf Iterum-1.0.0-Linux-x64.tar.gz`
3. Copy: `cp -r Iterum.vst3 ~/.vst3/`
4. Open a DAW and verify Iterum appears in the plugin list

## Troubleshooting

### Release workflow failed

1. Check the Actions tab for error details
2. If CI tests failed, the release will not proceed - fix the tests first
3. If installer creation failed, check the specific job logs

### Tag already exists

```bash
# Delete local tag
git tag -d v1.0.0

# Delete remote tag
git push origin :refs/tags/v1.0.0

# Create new tag
git tag v1.0.0
git push origin v1.0.0
```

### macOS Gatekeeper blocks installation

Without code signing, users must:
1. Right-click the .pkg file
2. Select "Open"
3. Click "Open" in the dialog

## Release Checklist

- [ ] Version updated in CMakeLists.txt
- [ ] Version committed and pushed to main
- [ ] CI passing on main branch
- [ ] Tag created with `v` prefix (e.g., `v1.0.0`)
- [ ] Tag pushed to origin
- [ ] Release workflow completed successfully
- [ ] All three installers attached to GitHub Release
- [ ] Each installer tested on its target platform
- [ ] Release notes added (optional but recommended)
