# VST3 SDK and VSTGUI Implementation Guide

This document captures hard-won insights about VST3 SDK and VSTGUI that are not obvious from the official documentation. These findings prevent repeating debugging sessions that waste hours.

**Version**: 1.0.0 | **Last Updated**: 2025-12-28

---

## Table of Contents

1. [Parameter Types and toPlain()](#1-parameter-types-and-toplain)
2. [COptionMenu and Discrete Parameters](#2-coptionmenu-and-discrete-parameters)
3. [UIViewSwitchContainer and template-switch-control](#3-uiviewswitchcontainer-and-template-switch-control)
4. [Feedback Loop Prevention](#4-feedback-loop-prevention)
5. [Dropdown Parameter Helper](#5-dropdown-parameter-helper)
6. [Common Pitfalls](#6-common-pitfalls)

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

## 6. Common Pitfalls

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
