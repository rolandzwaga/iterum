// ==============================================================================
// CloudFrame -- Phase 11 DataExchange Payload for the Cloud View
// ==============================================================================
// Spec:  specs/seraphis-phase11-ui/spec.md  (C-2)
// Plan:  specs/seraphis-phase11-ui/plan.md  (section 5.1)
//
// Producer: Processor::publishCloudFrame(), audio thread, ONCE per process() call.
// Consumer: Controller::onDataExchangeBlocksReceived() (UI thread).
// One-way. Nothing about editing travels on this queue (C-2 clause 5); the
// controller -> processor direction is IMessage (Seraphis::UI::EditMessage).
// ==============================================================================

#pragma once

#include <cstdint>

namespace Seraphis {

// Field order is NORMATIVE: it is what produces the pinned 808-byte layout and
// what both sides memcpy. Do NOT reorder to "remove" the 4 interior padding
// bytes -- the producer memsets the member frame to zero once in
// setupProcessing() and only field-assigns thereafter, so the padding is
// deterministically zero in every published block.
struct CloudFrame {                          // POD, little-endian, memcpy'd
    std::uint32_t sequence            = 0;    // monotonic; wrap is benign
    std::uint16_t activeVoices        = 0;    // SeraphisEngine::getActiveVoiceCount() (seraphis_engine.h:981)
    std::uint8_t  focusVoice          = 0;    // C-2 clause 4 (allocation-serial focus rule)
    std::uint8_t  partialCount        = 0;    // 0 .. HarmonicCloud::kMaxPartials (64, harmonic_cloud.h:138)
    float         fundamentalHz       = 0.0f; // focus voice's UNDETUNED f0 (no drift multiplier)
    float         voiceLevel          = 0.0f; // SeraphisEngine::getVoiceLevel(focus) (seraphis_engine.h:1003)
    float         morphTravelPosition = 0.0f; // SpectralMorphEngine::getTravelPosition() (spectral_morph_engine.h:441)
    float         frequencyHz[64]     = {};   // DRIFT-INCLUSIVE, C-2 clause 3
    float         amplitude  [64]     = {};   // display amplitude, C-2 clause 3
    float         position   [64]     = {};   // [-1, +1]
    std::uint64_t maskBits            = 0;    // bit i set <=> partial i masked
    std::uint64_t overriddenBits      = 0;    // bit i set <=> pan and/or mask override
};

// 8 (header) + 12 (three floats) + 768 (three float[64]) = 788, + 4 bytes of
// alignment padding before the first std::uint64_t (which forces alignof == 8)
// = 792, + 16 = 808.
static_assert(sizeof(CloudFrame) == 808, "C-2's pinned layout");

// User context ID for the CloudFrame queue (distinct from every other Krate
// DataExchange queue id).
inline constexpr std::uint32_t kCloudFrameUserContextId = 0x53434C44u;  // 'SCLD'

}  // namespace Seraphis
