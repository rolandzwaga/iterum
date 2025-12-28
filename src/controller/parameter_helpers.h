#pragma once

// ==============================================================================
// Parameter Helper Functions
// ==============================================================================
// These helpers ensure correct parameter types are used for common patterns.
// See specs/VST-GUIDE.md for why these helpers exist.
//
// KEY INSIGHT: Basic Parameter::toPlain() returns normalized value unchanged!
// StringListParameter::toPlain() properly scales to integer indices.
// ==============================================================================

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/base/ustring.h"
#include <initializer_list>

namespace Iterum {

// ==============================================================================
// createDropdownParameter - For COptionMenu / discrete list parameters
// ==============================================================================
// ALWAYS use this for dropdown menus instead of parameters.addParameter() with
// stepCount. This ensures toPlain() returns integer indices (0, 1, 2, ...)
// instead of normalized values (0.0 - 1.0).
//
// Usage:
//   parameters.addParameter(createDropdownParameter(
//       STR16("FFT Size"), kFFTSizeId,
//       {STR16("512"), STR16("1024"), STR16("2048"), STR16("4096")}
//   ));
//
// Why this exists:
//   Basic Parameter::toPlain() just returns the normalized value unchanged.
//   StringListParameter::toPlain() properly converts using FromNormalized().
//   Using the wrong parameter type causes dropdowns to malfunction for
//   indices > stepCount/2.
// ==============================================================================

inline Steinberg::Vst::StringListParameter* createDropdownParameter(
    const Steinberg::Vst::TChar* title,
    Steinberg::Vst::ParamID id,
    std::initializer_list<const Steinberg::Vst::TChar*> options) {

    auto* param = new Steinberg::Vst::StringListParameter(
        title,
        id,
        nullptr,  // units (not applicable for list)
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );

    for (const auto* option : options) {
        param->appendString(option);
    }

    return param;
}

// ==============================================================================
// createDropdownParameterWithDefault - Same as above but with custom default
// ==============================================================================
// Use when the default value should not be the first item (index 0).
//
// Usage:
//   parameters.addParameter(createDropdownParameterWithDefault(
//       STR16("Quality"), kQualityId,
//       1,  // default to second option (index 1)
//       {STR16("Low"), STR16("Medium"), STR16("High")}
//   ));
// ==============================================================================

inline Steinberg::Vst::StringListParameter* createDropdownParameterWithDefault(
    const Steinberg::Vst::TChar* title,
    Steinberg::Vst::ParamID id,
    int32_t defaultIndex,
    std::initializer_list<const Steinberg::Vst::TChar*> options) {

    auto* param = new Steinberg::Vst::StringListParameter(
        title,
        id,
        nullptr,
        Steinberg::Vst::ParameterInfo::kCanAutomate |
        Steinberg::Vst::ParameterInfo::kIsList
    );

    for (const auto* option : options) {
        param->appendString(option);
    }

    // Set default after adding strings so stepCount is known
    if (defaultIndex >= 0 && defaultIndex <= param->getInfo().stepCount) {
        param->setNormalized(param->toNormalized(static_cast<Steinberg::Vst::ParamValue>(defaultIndex)));
    }

    return param;
}

} // namespace Iterum
