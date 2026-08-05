// ==============================================================================
// EditMessage -- Phase 11 controller -> processor wire format (C-5)
// ==============================================================================
// Spec: specs/seraphis-phase11-ui/spec.md (C-5, FR-036, FR-047, FR-048)
// Plan: specs/seraphis-phase11-ui/plan.md section 6.1
//
// Producer: Controller (message thread) -- one IMessage per gesture step,
//   throttled to at most one per 33 ms with a mandatory terminal flush on
//   gesture-end (C-5 clause 3).
// Consumer: Processor::notify() (message thread). Never reached from process().
//
// THIS IS THE ONE HEADER UNDER src/ui/ THE PROCESSOR MAY INCLUDE. That is
// deliberate and is not a UI dependency: this file declares a POD and three
// constexpr strings, includes only <cstdint>, and names no VSTGUI type. It is
// the *wire format*, shared by both sides -- the same sanctioned shared-POD
// exception as Membrum's meters_block.h.
// ==============================================================================

#pragma once

#include <cstdint>

namespace Seraphis::UI {

inline constexpr const char* kSeraphisEditMessageId   = "SeraphisEdit";
inline constexpr const char* kSeraphisEditAttributeId = "payload";

/// Kind 8's SECOND binary attribute: a whole component-state stream, verbatim.
/// (Phase 12 hotfix amendment, 2026-08-05 - see kind 8 in the table below.)
inline constexpr const char* kSeraphisStateAttributeId = "state";

/// Upper bound on a kind-8 stream attribute. The v3 stream is 2868 bytes; the
/// cap is generous headroom for future appends, and anything beyond it is not a
/// Seraphis state and is dropped as malformed.
inline constexpr std::uint32_t kMaxPresetStateBytes = 65536;

// ------------------------------------------------------------------------------
// PER-KIND FIELD SEMANTICS (C-5's table -- normative)
//
//   kind 0  EditorGate       a = 1 open, a = 0 close. Sent by the CONTROLLER'S
//                            refcount ONLY on a 0->1 / 1->0 transition (Q7),
//                            never once per view. `slot` and `index` ignored;
//                            `b` unused, reserved 0.
//   kind 1  PartialRatioAmp  a = ratio, b = amplitude. `slot` = morph slot 0..3,
//                            `index` = partial 0..63.
//   kind 2  PartialPan       a = position in [-1, +1]. `index` = partial 0..63.
//                            `slot` ignored.
//   kind 3  PartialMask      a = 0/1, the TOGGLED value the controller computed
//                            from CloudFrame::maskBits (Q5) -- never an
//                            unconditional mask. `index` = partial 0..63.
//                            `slot` ignored.
//   kind 4  BlendStates      a = t, b = slot B index as float. `slot` =
//                            destination. Dropped if no live kind-7 snapshot
//                            exists for the current gesture (C-5 clause 5).
//   kind 5  TiltState        a = ABSOLUTE dB/oct (C-6), never a delta.
//                            `slot` = morph slot 0..3.
//   kind 6  SlotSelect       `slot` = newly selected slot. Ends any gesture.
//   kind 7  BlendBegin       b = slot B index as float -- the snapshot source
//                            for the gesture that follows (Q2); `slot` is the
//                            destination, unambiguous for kinds 4 and 7 alike.
//                            `a` unused, reserved 0.
//   kind 8  PresetState      Phase 12 hotfix amendment (2026-08-05, full-
//                            fidelity ruling): the preset browser's LOAD path.
//                            The POD carries no data (`slot`/`index`/`a`/`b`
//                            reserved 0); the message carries a SECOND binary
//                            attribute, kSeraphisStateAttributeId, holding a
//                            whole component-state stream verbatim. The
//                            processor applies it with its own setState() on
//                            the message thread -- the project-load path, so
//                            params, spectral payloads, the [partials] table
//                            WITH its override bits, the authoring-mirror
//                            trackers and the force-push all behave exactly as
//                            on project load. A kind-8 message without the
//                            attribute (or with a size outside
//                            [4, kMaxPresetStateBytes]) is dropped silently.
//
// Unknown `kind`, out-of-range `slot`/`index`, and non-finite `a`/`b` are
// dropped silently by Processor::notify (C-5 clause 5, FR-036) -- a message is
// untrusted input, and the mutators' own rejection is the second line of
// defence, not the first.
// ------------------------------------------------------------------------------

struct EditMessage {          // POD; moved as ONE binary attribute
    std::uint8_t  kind  = 0;  // 0 EditorGate, 1 PartialRatioAmp, 2 PartialPan, 3 PartialMask,
                              // 4 BlendStates, 5 TiltState, 6 SlotSelect, 7 BlendBegin,
                              // 8 PresetState
    std::uint8_t  slot  = 0;  // 0..3 morph slot (ignored by kinds 0, 2, 3, 8)
    std::uint16_t index = 0;  // partial index 0..63 (kinds 1, 2, 3)
    float         a     = 0.0f;
    float         b     = 0.0f;
};
static_assert(sizeof(EditMessage) == 12);

inline constexpr std::uint8_t kEditKindPresetState = 8;
inline constexpr std::uint8_t kEditKindCount = 9;

}  // namespace Seraphis::UI
