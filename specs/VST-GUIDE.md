# VST3 SDK and VSTGUI Implementation Guide

This document captures hard-won insights about VST3 SDK and VSTGUI that are not obvious from the official documentation. These findings prevent repeating debugging sessions that waste hours.

**Version**: 1.1.0 | **Last Updated**: 2025-12-29

---

## Table of Contents

1. [Parameter Types and toPlain()](#1-parameter-types-and-toplain)
2. [COptionMenu and Discrete Parameters](#2-coptionmenu-and-discrete-parameters)
3. [UIViewSwitchContainer and template-switch-control](#3-uiviewswitchcontainer-and-template-switch-control)
4. [Feedback Loop Prevention](#4-feedback-loop-prevention)
5. [Dropdown Parameter Helper](#5-dropdown-parameter-helper)
6. [Conditional Control Visibility (Thread-Safe Pattern)](#6-conditional-control-visibility-thread-safe-pattern)
7. [Common Pitfalls](#7-common-pitfalls)
8. [Cross-Platform Custom Views](#8-cross-platform-custom-views)

---

## 1. Parameter Types and toPlain()

### The Problem

The base `Parameter` class does NOT scale normalized values in `toPlain()`:

```cpp
// From vstparameters.cpp - Parameter::toPlain()
ParamValue Parameter::toPlain(ParamValue normValue) const {
    return normValue;  // Just returns the input unchanged!
}
```

This means if you create a parameter with `parameters.addParameter()` and expect `toPlain(0.5)` to return `5.0` for a 0-10 range, **it won't work**.

### The Solution

Use the appropriate parameter type for your use case:

| Use Case | Parameter Type | toPlain() Behavior |
|----------|---------------|-------------------|
| Continuous knob (0-1 range) | `Parameter` | Returns normalized value |
| Continuous range (e.g., 20Hz-20kHz) | `RangeParameter` | Scales to min/max range |
| Discrete list (e.g., modes, types) | `StringListParameter` | Returns 0, 1, 2, ... stepCount |

### StringListParameter Example

```cpp
// CORRECT: StringListParameter for discrete mode selection
auto* modeParam = new Steinberg::Vst::StringListParameter(
    STR16("Mode"),             // title
    kModeId,                   // parameter ID
    nullptr,                   // units
    Steinberg::Vst::ParameterInfo::kCanAutomate |
    Steinberg::Vst::ParameterInfo::kIsList
);
modeParam->appendString(STR16("Granular"));
modeParam->appendString(STR16("Spectral"));
// ... add all options
parameters.addParameter(modeParam);
```

### RangeParameter Example

```cpp
// CORRECT: RangeParameter for frequency range
auto* freqParam = new Steinberg::Vst::RangeParameter(
    STR16("Frequency"),        // title
    kFreqId,                   // parameter ID
    STR16("Hz"),              // units
    20.0,                      // minPlain
    20000.0,                   // maxPlain
    1000.0,                    // defaultPlain
    0,                         // stepCount (0 = continuous)
    Steinberg::Vst::ParameterInfo::kCanAutomate
);
parameters.addParameter(freqParam);
```

### Key Insight

**Never use custom formulas like `value * 10.0 + 0.5` to convert normalized values.** Always use the SDK's `param->toPlain(normalized)` after ensuring you're using the correct parameter type.

---

## 2. COptionMenu and Discrete Parameters

### Menu Population

VSTGUI's VST3Editor automatically populates COptionMenu items from the parameter's `stepCount` and calls `getParamStringByValue()` for each index.

### StringListParameter Handles toString Automatically

When you use `StringListParameter`, you don't need to override `getParamStringByValue()` for that parameter - the StringListParameter class handles it:

```cpp
// From vstparameters.cpp - StringListParameter::toString()
void StringListParameter::toString(ParamValue _valueNormalized, String128 string) const {
    int32 index = static_cast<int32>(toPlain(_valueNormalized));
    if (const TChar* valueString = strings.at(index)) {
        UString(string, str16BufferSize(String128)).assign(valueString);
    }
}
```

### COptionMenu Value Range

COptionMenu stores values as integers (0, 1, 2, ..., nbEntries-1). When bound to a parameter:
- `getValue()` returns the integer index
- `getValueNormalized()` returns index / (nbEntries - 1)

---

## 3. UIViewSwitchContainer and template-switch-control

### Automatic View Switching

UIViewSwitchContainer can automatically switch views based on a control's value using the `template-switch-control` attribute:

```xml
<view class="UIViewSwitchContainer"
      template-names="Panel1,Panel2,Panel3"
      template-switch-control="Mode"/>
```

This creates a `UIDescriptionViewSwitchController` that:
1. Listens to the control's valueChanged events
2. Converts normalized value to template index
3. Calls `setCurrentViewIndex()` automatically

### Index Calculation

The controller calculates the view index from the control's normalized value:

```cpp
// Simplified from UIDescriptionViewSwitchController
int32_t index = static_cast<int32_t>(normalizedValue * templateCount);
index = std::min(index, templateCount - 1);
```

### Requirements for Automatic Binding

1. The control must have a `control-tag` matching the parameter name
2. The parameter must be properly registered in the controller
3. For discrete parameters, use `StringListParameter` for correct value scaling

---

## 4. Feedback Loop Prevention

### Built-in Protection

VST3Editor already prevents feedback loops in `valueChanged()`:

```cpp
void VST3Editor::valueChanged(CControl* pControl) {
    if (!pControl->isEditing())  // Only propagates USER edits
        return;
    // ... send normalized value to host
}
```

### When isEditing() is True

- User is actively manipulating the control (mouse down, dragging)
- User just clicked a menu item

### When isEditing() is False

- Host is updating the control programmatically via `setParamNormalized()`
- State is being restored

### Implication

**You don't need custom settling time guards or feedback prevention code.** VSTGUI handles this automatically.

---

## 5. Dropdown Parameter Helper

### The Problem

Creating `StringListParameter` correctly requires multiple lines of code:

```cpp
auto* param = new Steinberg::Vst::StringListParameter(
    STR16("Mode"),
    kModeId,
    nullptr,
    Steinberg::Vst::ParameterInfo::kCanAutomate |
    Steinberg::Vst::ParameterInfo::kIsList
);
param->appendString(STR16("Option1"));
param->appendString(STR16("Option2"));
param->appendString(STR16("Option3"));
parameters.addParameter(param);
```

This is verbose and easy to get wrong (e.g., forgetting `kIsList` flag or accidentally using `parameters.addParameter()` with stepCount instead).

### The Solution

Use the `createDropdownParameter` helper from `src/controller/parameter_helpers.h`:

```cpp
#include "controller/parameter_helpers.h"

// Single line, impossible to get wrong
parameters.addParameter(createDropdownParameter(
    STR16("Mode"), kModeId,
    STR16("Option1"), STR16("Option2"), STR16("Option3")
));
```

### Helper Functions

**`createDropdownParameter`** - Default index 0:
```cpp
parameters.addParameter(createDropdownParameter(
    STR16("FFT Size"), kFFTSizeId,
    STR16("512"), STR16("1024"), STR16("2048"), STR16("4096")
));
```

**`createDropdownParameterWithDefault`** - Custom default index:
```cpp
parameters.addParameter(createDropdownParameterWithDefault(
    STR16("FFT Size"), kFFTSizeId,
    1,  // default to index 1 (1024)
    STR16("512"), STR16("1024"), STR16("2048"), STR16("4096")
));
```

### Implementation

The helper uses C++17 fold expressions for variadic templates:

```cpp
template<typename... Strings>
inline Steinberg::Vst::StringListParameter* createDropdownParameter(
    const Steinberg::Vst::TChar* title,
    Steinberg::Vst::ParamID id,
    Strings... options)
{
    auto* param = new Steinberg::Vst::StringListParameter(
        title, id, nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );
    (param->appendString(options), ...);  // C++17 fold expression
    return param;
}
```

### Benefits

1. **Impossible to create wrong type** - Always creates `StringListParameter`
2. **Impossible to forget flags** - `kCanAutomate` and `kIsList` are always set
3. **Less code** - Single line vs 7+ lines
4. **Self-documenting** - All options visible at registration point
5. **Compile-time checked** - Wrong argument types caught by compiler

### When to Use

**Always use for dropdowns.** There is no reason to manually create `StringListParameter` anymore.

| Scenario | Use |
|----------|-----|
| Mode selector (3-20 options) | `createDropdownParameter` |
| FFT size selection | `createDropdownParameterWithDefault` (if default != 0) |
| Filter type (LP/HP/BP) | `createDropdownParameter` |
| Any discrete list | `createDropdownParameter` |

### Formatting Not Required

`StringListParameter::toString()` automatically handles value-to-string conversion. **Do not** add custom formatting cases for dropdown parameters:

```cpp
// NOT NEEDED - StringListParameter handles this automatically
case kModeId: {
    const char* names[] = {"Granular", "Spectral", ...};
    // ...
}

// Just leave a comment in formatXxxParam():
// kModeId: handled by StringListParameter::toString() automatically
```

---

## 6. Conditional Control Visibility (Thread-Safe Pattern)

### The Problem

Sometimes UI controls should only be visible when another parameter has a specific value. For example:
- Delay time control should be hidden when time mode is "Synced" (since the time value is ignored)
- Filter controls should be hidden when filter is bypassed

**CRITICAL**: `setParamNormalized()` can be called from **ANY thread**:
- User interaction → UI thread
- Automation → host thread (could be audio thread!)
- State loading → background thread
- Host sync → any thread

**VSTGUI controls MUST only be manipulated on the UI thread.** Calling `setVisible()` or any other control method from a non-UI thread causes:
- Race conditions
- Host hangs and crashes
- Undefined behavior

### The WRONG Pattern (Thread Safety Violation)

```cpp
// BROKEN - DO NOT DO THIS
Steinberg::tresult PLUGIN_API Controller::setParamNormalized(
    Steinberg::Vst::ParamID id,
    Steinberg::Vst::ParamValue value) {

    auto result = EditControllerEx1::setParamNormalized(id, value);

    // Thread safety violation - setParamNormalized can be called from any thread!
    if (id == kTimeModeId) {
        bool shouldBeVisible = (value < 0.5f);
        delayTimeControl_->setVisible(shouldBeVisible);  // CRASH!
    }

    return result;
}
```

**Why This Crashes:**
1. Automation writes parameter changes → calls `setParamNormalized()` on audio thread
2. Audio thread calls `setVisible()` on VSTGUI control
3. UI thread is simultaneously rendering the control
4. Race condition → crash or hang

### The CORRECT Pattern (IDependent + Deferred Updates)

Use the `IDependent` mechanism with `UpdateHandler` for automatic thread-safe UI updates:

```cpp
// In controller.cpp - create a visibility controller class
class VisibilityController : public Steinberg::FObject {
public:
    VisibilityController(
        Steinberg::Vst::EditController* editController,
        Steinberg::Vst::Parameter* timeModeParam,
        VSTGUI::CControl* delayTimeControl)
    : editController_(editController)
    , timeModeParam_(timeModeParam)
    , delayTimeControl_(delayTimeControl)
    {
        if (timeModeParam_) {
            timeModeParam_->addRef();
            timeModeParam_->addDependent(this);  // Register for notifications
            timeModeParam_->deferUpdate();       // Trigger initial update on UI thread
        }
        if (delayTimeControl_) {
            delayTimeControl_->remember();
        }
    }

    ~VisibilityController() override {
        if (timeModeParam_) {
            timeModeParam_->removeDependent(this);
            timeModeParam_->release();
        }
        if (delayTimeControl_) {
            delayTimeControl_->forget();
        }
    }

    // IDependent::update - AUTOMATICALLY called on UI thread!
    void PLUGIN_API update(Steinberg::FUnknown* changedUnknown, Steinberg::int32 message) override {
        if (message == IDependent::kChanged && timeModeParam_ && delayTimeControl_) {
            float normalizedValue = timeModeParam_->getNormalized();
            bool shouldBeVisible = (normalizedValue < 0.5f);

            // SAFE: This is called on UI thread via UpdateHandler::deferedUpdate()
            delayTimeControl_->setVisible(shouldBeVisible);

            if (delayTimeControl_->getFrame()) {
                delayTimeControl_->invalid();
            }
        }
    }

    OBJ_METHODS(VisibilityController, FObject)

private:
    Steinberg::Vst::EditController* editController_;
    Steinberg::Vst::Parameter* timeModeParam_;
    VSTGUI::CControl* delayTimeControl_;
};
```

### How It Works

1. **Parameter Registration**: `timeModeParam_->addDependent(this)` registers the visibility controller to receive parameter change notifications

2. **Deferred Updates**: When the parameter changes (from ANY thread), it calls `deferUpdate()` internally, which schedules the notification to run on the UI thread via `UpdateHandler`

3. **UI Thread Callback**: The `update()` method is called on the UI thread at 30Hz by a timer (`CVSTGUITimer` in `vst3editor.cpp`)

4. **Safe UI Manipulation**: Since `update()` runs on the UI thread, it's safe to call `setVisible()`, `invalid()`, or any other control method

### Integration in Controller

```cpp
// In controller.h
class Controller : public Steinberg::Vst::EditControllerEx1,
                   public VSTGUI::VST3EditorDelegate {
private:
    // Visibility controllers for conditional control visibility (thread-safe)
    Steinberg::IPtr<Steinberg::FObject> digitalVisibilityController_;
    Steinberg::IPtr<Steinberg::FObject> pingPongVisibilityController_;
};

// In controller.cpp - didOpen()
void Controller::didOpen(VSTGUI::VST3Editor* editor) {
    activeEditor_ = editor;

    if (editor) {
        if (auto* frame = editor->getFrame()) {
            // Find controls in view hierarchy (helper function)
            auto findControl = [frame](int32_t tag) -> VSTGUI::CControl* {
                // ... traversal code ...
            };

            // Create visibility controllers for Digital mode
            if (auto* digitalDelayTime = findControl(kDigitalDelayTimeId)) {
                if (auto* digitalTimeMode = getParameterObject(kDigitalTimeModeId)) {
                    digitalVisibilityController_ = new VisibilityController(
                        this, digitalTimeMode, digitalDelayTime);
                }
            }

            // Create visibility controllers for PingPong mode
            if (auto* pingPongDelayTime = findControl(kPingPongDelayTimeId)) {
                if (auto* pingPongTimeMode = getParameterObject(kPingPongTimeModeId)) {
                    pingPongVisibilityController_ = new VisibilityController(
                        this, pingPongTimeMode, pingPongDelayTime);
                }
            }
        }
    }
}

// In controller.cpp - willClose()
void Controller::willClose(VSTGUI::VST3Editor* editor) {
    // Clean up (automatically removes dependents and releases refs)
    digitalVisibilityController_ = nullptr;
    pingPongVisibilityController_ = nullptr;
    activeEditor_ = nullptr;
}
```

### Key Benefits

1. **Thread-Safe**: Updates always happen on UI thread via `UpdateHandler`
2. **Automatic**: No manual synchronization or locks needed
3. **Framework-Native**: Uses VST3 SDK's built-in dependent notification system
4. **Clean Lifecycle**: Controllers are created in `didOpen()` and cleaned up in `willClose()`
5. **Works with Automation**: Handles automation, state loading, and user interaction identically

### UpdateHandler Internals

From `vst3editor.cpp`:

```cpp
// VST3Editor sets up a 30Hz timer for deferred updates
class IdleUpdateHandler {
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;

    static void start() {
        instance.timer = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [] (VSTGUI::CVSTGUITimer*) {
                gUpdateHandlerInit.get()->triggerDeferedUpdates();  // Runs on UI thread
            },
            1000 / 30);  // 30Hz
    }
};
```

When `parameter->deferUpdate()` is called (from any thread), it:
1. Marks the parameter as "needs update"
2. The 30Hz timer triggers `triggerDeferedUpdates()` on the UI thread
3. Calls `update()` on all registered dependents

### Alternative: IControlListener (UI-Only)

If you only need to respond to **user interaction** (not automation), you can implement `IControlListener`:

```cpp
class MyController : public IController, public IControlListener {
    void valueChanged(CControl* pControl) override {
        if (pControl->getTag() == kTimeModeId) {
            float value = pControl->getValue();
            bool shouldBeVisible = (value < 0.5f);
            delayTimeControl_->setVisible(shouldBeVisible);  // Safe - always on UI thread
        }
    }
};
```

**Limitation**: `valueChanged()` is ONLY called for user interaction, NOT for automation or state loading. For comprehensive visibility management, use the `IDependent` pattern.

### Testing Thread Safety

To verify thread safety:

1. **Automation Test**: Record automation for the controlling parameter, play it back while switching modes
2. **State Loading**: Save and restore plugin state multiple times while changing visibility-controlled parameters
3. **Stress Test**: Rapidly switch modes while automation is playing

If the plugin crashes, hangs, or exhibits race conditions, the pattern is not thread-safe.

---

## 7. Common Pitfalls

### Pitfall 1: Using Basic Parameter for Discrete Values

**Wrong:**
```cpp
parameters.addParameter(STR16("Mode"), nullptr, 10, 0.0, kCanAutomate, kModeId);
```
This creates a basic `Parameter` where `toPlain()` returns the normalized value unchanged.

**Right:**
```cpp
auto* modeParam = new StringListParameter(STR16("Mode"), kModeId, ...);
```

### Pitfall 2: Custom Normalization Formulas

**Wrong:**
```cpp
int modeIndex = static_cast<int>(valueNormalized * 10.0 + 0.5);
```
This reinvents the wheel and may have edge case bugs.

**Right:**
```cpp
auto* param = getParameterObject(kModeId);
int modeIndex = static_cast<int>(param->toPlain(valueNormalized));
```

### Pitfall 3: Manual View Switching

**Wrong:**
```cpp
// In setParamNormalized override
viewSwitchContainer_->setCurrentViewIndex(modeIndex);
```
Duplicates VSTGUI's built-in functionality and can cause race conditions.

**Right:**
Use `template-switch-control` attribute in editor.uidesc and let VSTGUI handle it.

### Pitfall 4: Overriding getParamStringByValue for StringListParameter

**Wrong:**
```cpp
case kModeId: {
    const char* modeNames[] = {"Granular", "Spectral", ...};
    // Manual string lookup
}
```
Duplicates what StringListParameter already does.

**Right:**
Don't override - let the base class delegate to StringListParameter's toString().

---

## 8. Cross-Platform Custom Views

When creating custom VSTGUI views (e.g., preset browsers, visualizers, custom controls), follow these rules to ensure cross-platform compatibility.

### Core Principle

**VSTGUI is inherently cross-platform.** All custom views MUST use only VSTGUI abstractions. Platform-specific code (Win32, Cocoa, GTK) is FORBIDDEN in UI code.

### Cross-Platform Component Reference

| Component | Purpose | Cross-Platform |
|-----------|---------|----------------|
| `CView` / `CControl` | Base classes for custom views | ✅ |
| `CDataBrowser` | List/table views | ✅ |
| `CNewFileSelector` | File open/save dialogs | ✅ |
| `COptionMenu` | Dropdown menus | ✅ |
| `CScrollView` | Scrollable containers | ✅ |
| `CDrawContext` | All drawing operations | ✅ |

### Rule 1: Drawing - VSTGUI Primitives Only

```cpp
// ✅ CORRECT - Use CDrawContext methods
void MyView::draw(CDrawContext* context) {
    context->setFillColor(CColor(40, 40, 40, 255));
    context->drawRect(getViewSize(), kDrawFilled);
    context->setFont(kNormalFont);
    context->drawString("Preset Name", textRect, kLeftText);
}

// ❌ FORBIDDEN - Platform-specific drawing
#ifdef _WIN32
    HDC hdc = GetDC(hwnd);  // NO!
#endif
#ifdef __APPLE__
    [[NSGraphicsContext currentContext] ...];  // NO!
#endif
```

### Rule 2: File Paths - Use std::filesystem

```cpp
#include <filesystem>
namespace fs = std::filesystem;

// ✅ CORRECT - Platform-agnostic path handling
fs::path getPresetDirectory() {
    #if defined(_WIN32)
        const char* docs = std::getenv("USERPROFILE");
        return fs::path(docs) / "Documents" / "VST3 Presets" / "Iterum";
    #elif defined(__APPLE__)
        const char* home = std::getenv("HOME");
        return fs::path(home) / "Library" / "Audio" / "Presets" / "Iterum";
    #else  // Linux
        const char* home = std::getenv("HOME");
        return fs::path(home) / ".vst3" / "presets" / "Iterum";
    #endif
}

// ✅ Path operations are cross-platform
fs::path presetPath = getPresetDirectory() / "Digital" / "My Preset.vstpreset";
bool exists = fs::exists(presetPath);
fs::create_directories(presetPath.parent_path());

// ❌ FORBIDDEN - Platform-specific path APIs in UI code
#ifdef _WIN32
    SHGetKnownFolderPath(FOLDERID_Documents, ...);  // Isolate in platform/ folder
#endif
```

### Rule 3: File Dialogs - CNewFileSelector Only

```cpp
// ✅ CORRECT - Cross-platform file dialog
void PresetBrowserView::onImportClicked() {
    auto selector = CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectFile);
    selector->setTitle("Import Preset");
    selector->addFileExtension(CFileExtension("VST3 Preset", "vstpreset"));

    selector->run([this](CNewFileSelector* sel) {
        if (sel->getNumSelectedFiles() > 0) {
            UTF8StringPtr path = sel->getSelectedFile(0);
            presetManager_->importPreset(path);
        }
    });
    selector->forget();
}

// ❌ FORBIDDEN - Platform-specific dialogs
#ifdef _WIN32
    GetOpenFileName(&ofn);  // NO!
#endif
#ifdef __APPLE__
    NSSavePanel* panel = ...;  // NO!
#endif
```

### Rule 4: Fonts - Use VSTGUI Font System

```cpp
// ✅ CORRECT - VSTGUI font constants
context->setFont(kNormalFont);
context->setFont(kNormalFontBold);
context->setFont(kSystemFont);

// ✅ CORRECT - Generic family names (fallback gracefully)
auto font = makeOwned<CFontDesc>("Arial", 12);

// ⚠️ CAUTION - Platform-specific fonts need fallbacks
#if defined(_WIN32)
    auto font = makeOwned<CFontDesc>("Segoe UI", 12);
#elif defined(__APPLE__)
    auto font = makeOwned<CFontDesc>("SF Pro", 12);
#else
    auto font = makeOwned<CFontDesc>("DejaVu Sans", 12);
#endif
// Better: Just use kNormalFont or generic "Arial"
```

### Rule 5: Mouse/Keyboard - Use VSTGUI Events

```cpp
// ✅ CORRECT - VSTGUI event handling
CMouseEventResult MyView::onMouseDown(CPoint& where, const CButtonState& buttons) {
    if (buttons.isLeftButton() && buttons.isDoubleClick()) {
        loadSelectedPreset();
    }
    return kMouseEventHandled;
}

void MyView::onKeyboardEvent(KeyboardEvent& event) {
    if (event.character == 'f' && event.modifiers.has(ModifierKey::Control)) {
        openSearchField();
        event.consumed = true;
    }
}

// ❌ FORBIDDEN - Platform-specific input
#ifdef _WIN32
    if (GetAsyncKeyState(VK_LBUTTON)) { }  // NO!
#endif
```

### Rule 6: String Handling - UTF-8 Throughout

```cpp
// ✅ CORRECT - UTF-8 strings
std::string presetName = "Café Delay";  // UTF-8 encoded
context->drawString(presetName.c_str(), rect, kLeftText);

// For VST3 API calls requiring UTF-16:
Steinberg::String str;
str.fromUTF8(presetName.c_str());

// ❌ AVOID - Platform-specific string types
#ifdef _WIN32
    std::wstring wstr = L"...";  // Windows-specific wide strings
#endif
```

### Isolating Platform-Specific Code

When platform differences are unavoidable (e.g., preset path discovery), isolate them:

```
src/
├── platform/
│   ├── preset_paths.h           // Interface
│   ├── preset_paths_win.cpp     // Windows implementation
│   ├── preset_paths_mac.cpp     // macOS implementation
│   └── preset_paths_linux.cpp   // Linux implementation
└── ui/
    └── preset_browser.cpp       // Uses Platform:: interface only
```

```cpp
// src/platform/preset_paths.h
namespace Platform {
    std::filesystem::path getUserPresetDirectory();
    std::filesystem::path getFactoryPresetDirectory();
}

// UI code uses only the interface
#include "platform/preset_paths.h"
auto presets = scanDirectory(Platform::getUserPresetDirectory());
```

### Cross-Platform Testing Checklist

Before merging any custom view code:

- [ ] Zero platform-specific includes in UI files (`windows.h`, `Cocoa/Cocoa.h`, etc.)
- [ ] All drawing uses `CDrawContext` methods only
- [ ] All file dialogs use `CNewFileSelector`
- [ ] All paths use `std::filesystem::path`
- [ ] Fonts use VSTGUI constants or generic family names
- [ ] Builds successfully on Windows, macOS, and Linux
- [ ] Tested visually on at least 2 platforms
- [ ] pluginval passes on all platforms

### Common Cross-Platform Pitfalls

| Issue | Platform | Solution |
|-------|----------|----------|
| Path separators | Windows uses `\` | Use `std::filesystem::path` (handles automatically) |
| Case sensitivity | Linux is case-sensitive | Consistent lowercase for preset folders |
| Hidden files | Linux/macOS `.` prefix | Don't start names with `.` |
| Line endings | Windows CRLF | Use binary mode for preset files |
| Font metrics | All differ slightly | Test text layout on all platforms |
| HiDPI scaling | All handle differently | Use VSTGUI scaling, test at 100%/200% |
| File permissions | Linux strict | Check `fs::permissions()` before write |
| Symbolic links | macOS/Linux common | Use `fs::canonical()` to resolve |

### CDataBrowser Cross-Platform Notes

`CDataBrowser` is fully cross-platform but has some considerations:

```cpp
// Row height may render differently due to font metrics
CCoord dbGetRowHeight(CDataBrowser* browser) override {
    return 24;  // Use fixed heights, not font-derived
}

// Scrollbar width varies by platform
CDataBrowser(size, delegate, style,
    16,  // Explicit scrollbar width for consistency
    nullptr);

// Selection colors should be explicit, not system-derived
if (flags & kRowSelected) {
    context->setFillColor(CColor(100, 150, 255, 150));  // Explicit color
    // NOT: context->setFillColor(kSystemSelectionColor);
}
```

### CNewFileSelector Platform Behavior

`CNewFileSelector` wraps native dialogs but behavior differs slightly:

| Behavior | Windows | macOS | Linux |
|----------|---------|-------|-------|
| Dialog appearance | Native Windows | Native Cocoa | GTK (if available) |
| Multiple extensions | All shown | All shown | May show separately |
| Initial directory | Respected | Respected | May be ignored |
| Default filename | Respected | Respected | Respected |

**Recommendation:** Always set `setInitialDirectory()` and `setDefaultSaveName()` but don't rely on exact behavior. Test on all platforms.

---

## Framework Philosophy

When something doesn't work with VSTGUI or VST3 SDK:

1. **The framework is correct** - It's used in thousands of commercial plugins
2. **You are using it wrong** - The bug is in your usage, not the framework
3. **Read the source** - The SDK and VSTGUI are open source; read them
4. **Use SDK functions** - Don't reinvent conversions the SDK already provides
5. **Trust automatic bindings** - template-switch-control, menu population, etc.

---

## Debugging Checklist

When VSTGUI/VST3 features don't work:

1. [ ] Add logging to trace actual values at each step
2. [ ] Check which parameter type you're using (`Parameter` vs `StringListParameter` vs `RangeParameter`)
3. [ ] Verify `toPlain()` returns expected values
4. [ ] Read the VSTGUI source in `extern/vst3sdk/vstgui4/vstgui/`
5. [ ] Read the VST3 SDK source in `extern/vst3sdk/public.sdk/source/vst/`
6. [ ] Check if automatic bindings are configured correctly in editor.uidesc
7. [ ] Verify control-tag names match parameter registration

---

## Source Code Locations

| Component | Location |
|-----------|----------|
| Parameter classes | `extern/vst3sdk/public.sdk/source/vst/vstparameters.cpp` |
| VST3Editor | `extern/vst3sdk/vstgui4/vstgui/plugin-bindings/vst3editor.cpp` |
| UIViewSwitchContainer | `extern/vst3sdk/vstgui4/vstgui/uidescription/uiviewswitchcontainer.cpp` |
| COptionMenu | `extern/vst3sdk/vstgui4/vstgui/lib/controls/coptionmenu.cpp` |

---

## Incident Log

### 2025-12-28: Mode Switching Bug

**Symptom:** Mode dropdown (COptionMenu) modes 5-10 all switched to Spectral (index 1).

**Root Cause:** Mode parameter was created with `parameters.addParameter()` which creates a basic `Parameter`. The `toPlain()` method of basic `Parameter` just returns the normalized value unchanged, so `toPlain(0.5)` returned `0.5` instead of `5.0`.

**Solution:** Changed to `StringListParameter` which properly implements `toPlain()` using `FromNormalized()` to convert 0.0-1.0 to integer indices 0-10.

**Time Wasted:** Multiple hours due to pivoting between approaches without deeply investigating the actual value flow.

**Lesson:** Always check which parameter type you're using and verify `toPlain()` returns the expected value.

### 2025-12-28: Universal Dropdown Fix

**Symptom:** After fixing Mode dropdown, discovered 17+ other dropdown parameters across 9 files had the same issue.

**Files Affected:**
- `bbd_params.h` - Era (4 options)
- `spectral_params.h` - FFT Size (4), Spread Direction (3)
- `digital_params.h` - Time Mode (3), Note Value (11), Limiter Character (4), Era (4), Mod Waveform (4)
- `granular_params.h` - Envelope Type (4)
- `pingpong_params.h` - Time Mode (3), Note Value (11), L/R Ratio (5)
- `reverse_params.h` - Playback Mode (3), Filter Type (3)
- `freeze_params.h` - Filter Type (3)
- `multitap_params.h` - Timing Pattern (20), Spatial Pattern (7)
- `ducking_params.h` - Duck Target (3)

**Solution:** Created `src/controller/parameter_helpers.h` with `createDropdownParameter()` and `createDropdownParameterWithDefault()` helper functions. Updated all 9 parameter files to use these helpers.

**Prevention:** The helper pattern makes it impossible to accidentally create dropdown parameters with the wrong type. Future developers cannot make this mistake because the correct type is enforced by the helper function.

### 2025-12-29: Conditional Visibility Thread Safety Crash

**Symptom:** Plugin hangs and crashes when switching between Digital and PingPong modes while changing time mode between "Free" and "Synced". Delay time controls would initially show/hide correctly, then stop responding, then crash the host.

**Root Cause:** Called `setVisible()` on VSTGUI controls directly from `setParamNormalized()`, which can be called from ANY thread (automation, state loading, etc.). VSTGUI controls MUST only be manipulated on the UI thread. Direct manipulation from non-UI threads causes race conditions with the UI rendering thread.

**Solution:** Implemented `VisibilityController` class using VST3's `IDependent` mechanism with `UpdateHandler`. The pattern:
1. Register as dependent on the controlling parameter via `addDependent()`
2. When parameter changes (any thread), it calls `deferUpdate()`
3. `UpdateHandler` schedules `update()` callback to run on UI thread at 30Hz
4. `update()` method safely calls `setVisible()` on UI thread

**Time Wasted:** Multiple hours initially trying to add mode filtering and other workarounds without understanding the fundamental threading violation. Only after deep research into VSTGUI source code (vst3editor.cpp) was the correct pattern discovered.

**Lesson:** NEVER manipulate VSTGUI controls from `setParamNormalized()` or any method that can be called from non-UI threads. ALWAYS use `IDependent` with deferred updates for parameter-driven UI changes. The VST3 SDK provides this mechanism specifically to solve this threading problem.

**Sources:**
- [VSTGUI VST3Editor sets kDirtyCallAlwaysOnMainThread](https://github.com/steinbergmedia/vstgui/blob/develop/vstgui/plugin-bindings/vst3editor.cpp)
- [VST3 setParamNormalized must be called on UI thread discussion](https://forums.steinberg.net/t/vst3-hosting-when-to-use-ieditcontroller-setparamnormalized/787800)
- [JUCE added thread safety for setParamNormalized](https://github.com/juce-framework/JUCE/commit/9f03bbc358d67a3e0d0e3d7082259a4155aebd85)
