// ==============================================================================
// Factory Preset Generator for Iterum
// ==============================================================================
// Generates .vstpreset files for all delay modes with musically useful settings.
// Run this tool once during development to create factory presets.
//
// Build: Add to CMakeLists.txt as a separate executable, or compile standalone:
//   cl /EHsc /std:c++20 preset_generator.cpp -I../extern/vst3sdk /link
// ==============================================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstring>

// Simple binary writer that mimics IBStreamer for preset state
class BinaryWriter {
public:
    std::vector<uint8_t> data;

    void writeInt32(int32_t val) {
        auto bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }

    void writeFloat(float val) {
        auto bytes = reinterpret_cast<const uint8_t*>(&val);
        data.insert(data.end(), bytes, bytes + 4);
    }
};

// ==============================================================================
// Delay Mode Enum
// ==============================================================================
enum class DelayMode : int {
    Granular = 0,
    Spectral = 1,
    Shimmer = 2,
    Tape = 3,
    BBD = 4,
    Digital = 5,
    PingPong = 6,
    Reverse = 7,
    MultiTap = 8,
    Freeze = 9,
    Ducking = 10
};

// ==============================================================================
// Parameter Structs (simplified - no atomics needed for generation)
// ==============================================================================

struct GranularPreset {
    float grainSize = 100.0f;      // 5-500ms
    float density = 10.0f;          // 1-50
    float delayTime = 200.0f;       // 0-5000ms
    float pitch = 0.0f;             // -24 to +24 semitones
    float pitchSpray = 0.0f;        // 0-24 semitones
    float positionSpray = 0.0f;     // 0-1
    float panSpray = 0.0f;          // 0-1
    float reverseProb = 0.0f;       // 0-1
    int freeze = 0;                 // 0/1
    float feedback = 0.0f;          // 0-1.2
    float dryWet = 0.5f;            // 0-1 (params file stores 0-1, not 0-100)
    int envelopeType = 1;           // 0-2
    int timeMode = 0;               // 0=Free, 1=Synced
    int noteValue = 4;              // 0-9
    float jitter = 0.0f;            // 0-1
    int pitchQuantMode = 0;         // 0-3
    float texture = 0.5f;           // 0-1
    float stereoWidth = 0.5f;       // 0-1
};

struct SpectralPreset {
    int fftSize = 1024;
    float baseDelay = 250.0f;
    float spread = 0.0f;
    int spreadDirection = 0;
    float feedback = 0.0f;
    float feedbackTilt = 0.0f;
    int freeze = 0;
    float diffusion = 0.0f;
    float dryWet = 0.5f;            // 0-1 (params file stores 0-1, not 0-100)
    int spreadCurve = 0;
    float stereoWidth = 0.0f;
    int timeMode = 0;
    int noteValue = 4;
};

struct ShimmerPreset {
    float delayTime = 500.0f;
    float pitchSemitones = 12.0f;
    float pitchCents = 0.0f;
    float shimmerMix = 1.0f;            // 0-1 (params file stores 0-1, not 0-100)
    float feedback = 0.5f;
    float diffusionAmount = 0.5f;       // 0-1 (params file stores 0-1, not 0-100)
    float diffusionSize = 50.0f;
    int filterEnabled = 0;
    float filterCutoff = 4000.0f;
    float dryWet = 0.5f;                // 0-1 (params file stores 0-1, not 0-100)
    int timeMode = 0;     // 0=Free, 1=Synced
    int noteValue = 4;    // 0-9 (default: 1/8)
};

struct TapePreset {
    float motorSpeed = 1.0f;        // 0.5-2.0
    float motorInertia = 0.5f;      // 0-1
    float wear = 0.0f;              // 0-1
    float saturation = 0.0f;        // 0-1
    float age = 0.0f;               // 0-1
    int spliceEnabled = 0;
    float spliceIntensity = 0.5f;   // 0-1
    float feedback = 0.3f;          // 0-1.2
    float mix = 0.5f;               // 0-1 (params file stores 0-1, not 0-100)
    int head1Enabled = 1;
    float head1Level = 1.0f;
    float head1Pan = 0.0f;          // -1 to +1
    int head2Enabled = 0;
    float head2Level = 0.7f;
    float head2Pan = -0.5f;
    int head3Enabled = 0;
    float head3Level = 0.5f;
    float head3Pan = 0.5f;
};

struct BBDPreset {
    float delayTime = 300.0f;       // 1-1000ms
    float feedback = 0.4f;          // 0-1.2
    float modulationDepth = 0.3f;   // 0-1
    float modulationRate = 0.5f;    // 0.1-10Hz
    float age = 0.3f;               // 0-1
    int era = 1;                    // 0-2 (70s, 80s, Modern)
    float mix = 0.5f;               // 0-1 (params file stores 0-1, not 0-100)
    int timeMode = 0;               // 0=Free, 1=Synced
    int noteValue = 4;              // 0-9 (default: 1/8)
};

struct DigitalPreset {
    float delayTime = 500.0f;       // 1-10000ms
    int timeMode = 1;               // 0=Free, 1=Synced
    int noteValue = 4;              // 0-9
    float feedback = 0.4f;          // 0-1.2
    int limiterCharacter = 1;       // 0-2
    int era = 2;                    // 0-2
    float age = 0.0f;               // 0-1
    float modulationDepth = 0.0f;   // 0-1
    float modulationRate = 1.0f;    // 0.1-10Hz
    int modulationWaveform = 0;     // 0-2
    float mix = 0.5f;               // 0-1 (params file stores 0-1, not 0-100)
    float width = 100.0f;           // 0-200%
};

struct PingPongPreset {
    float delayTime = 500.0f;
    int timeMode = 1;
    int noteValue = 4;
    int lrRatio = 0;                // 0-6
    float feedback = 0.5f;
    float crossFeedback = 1.0f;
    float width = 100.0f;
    float modulationDepth = 0.0f;
    float modulationRate = 1.0f;
    float mix = 0.5f;               // 0-1
};

struct ReversePreset {
    float chunkSize = 500.0f;
    float crossfade = 0.5f;         // 0-1 (params file stores 0-1, not 0-100)
    int playbackMode = 0;           // 0-2
    float feedback = 0.0f;
    int filterEnabled = 0;
    float filterCutoff = 4000.0f;
    int filterType = 0;
    float dryWet = 0.5f;            // 0-1
    int timeMode = 0;               // 0=Free, 1=Synced
    int noteValue = 4;              // 0-9 (default: 1/8)
};

struct MultiTapPreset {
    int timingPattern = 2;          // 0-19
    int spatialPattern = 2;         // 0-6
    int tapCount = 4;               // 2-16
    float baseTime = 500.0f;
    float tempo = 120.0f;
    float feedback = 0.5f;          // 0-1.1
    float feedbackLPCutoff = 20000.0f;
    float feedbackHPCutoff = 20.0f;
    float morphTime = 500.0f;
    float dryWet = 0.5f;            // 0-1 (params file stores 0-1, not 0-100)
    int timeMode = 0;               // 0=Free, 1=Synced
    int noteValue = 4;              // 0-9 (default: 1/8)
};

struct FreezePreset {
    int freezeEnabled = 0;
    float delayTime = 500.0f;
    float feedback = 0.5f;
    float pitchSemitones = 0.0f;
    float pitchCents = 0.0f;
    float shimmerMix = 0.0f;
    float decay = 0.5f;
    float diffusionAmount = 0.3f;
    float diffusionSize = 0.5f;
    int filterEnabled = 0;
    int filterType = 0;
    float filterCutoff = 1000.0f;
    float dryWet = 0.5f;
    int timeMode = 0;               // 0=Free, 1=Synced
    int noteValue = 4;              // 0-9 (default: 1/8)
};

struct DuckingPreset {
    int duckingEnabled = 1;
    float threshold = -30.0f;
    float duckAmount = 0.5f;        // 0-1 (params file stores 0-1, not 0-100)
    float attackTime = 10.0f;
    float releaseTime = 200.0f;
    float holdTime = 50.0f;
    int duckTarget = 0;
    int sidechainFilterEnabled = 0;
    float sidechainFilterCutoff = 80.0f;
    float delayTime = 500.0f;
    float feedback = 0.0f;
    float dryWet = 0.5f;            // 0-1 (params file stores 0-1, not 0-100)
    int timeMode = 0;               // 0=Free, 1=Synced
    int noteValue = 4;              // 0-9 (default: 1/8)
};

// ==============================================================================
// Preset Definition
// ==============================================================================

struct PresetDef {
    std::string name;
    std::string category;
    DelayMode mode;

    // Union-like storage for different preset types
    GranularPreset granular;
    SpectralPreset spectral;
    ShimmerPreset shimmer;
    TapePreset tape;
    BBDPreset bbd;
    DigitalPreset digital;
    PingPongPreset pingpong;
    ReversePreset reverse;
    MultiTapPreset multitap;
    FreezePreset freeze;
    DuckingPreset ducking;
};

// ==============================================================================
// State Serialization (matches processor.cpp format exactly)
// ==============================================================================

void writeGranularState(BinaryWriter& w, const GranularPreset& p) {
    w.writeFloat(p.grainSize);
    w.writeFloat(p.density);
    w.writeFloat(p.delayTime);
    w.writeFloat(p.pitch);
    w.writeFloat(p.pitchSpray);
    w.writeFloat(p.positionSpray);
    w.writeFloat(p.panSpray);
    w.writeFloat(p.reverseProb);
    w.writeInt32(p.freeze);
    w.writeFloat(p.feedback);
    w.writeFloat(p.dryWet);
    w.writeInt32(p.envelopeType);
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
    w.writeFloat(p.jitter);
    w.writeInt32(p.pitchQuantMode);
    w.writeFloat(p.texture);
    w.writeFloat(p.stereoWidth);
}

void writeSpectralState(BinaryWriter& w, const SpectralPreset& p) {
    w.writeInt32(p.fftSize);
    w.writeFloat(p.baseDelay);
    w.writeFloat(p.spread);
    w.writeInt32(p.spreadDirection);
    w.writeFloat(p.feedback);
    w.writeFloat(p.feedbackTilt);
    w.writeInt32(p.freeze);
    w.writeFloat(p.diffusion);
    w.writeFloat(p.dryWet);
    w.writeInt32(p.spreadCurve);
    w.writeFloat(p.stereoWidth);
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
}

void writeShimmerState(BinaryWriter& w, const ShimmerPreset& p) {
    // Order MUST match shimmer_params.h saveShimmerParams()
    w.writeFloat(p.delayTime);
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
    w.writeFloat(p.pitchSemitones);
    w.writeFloat(p.pitchCents);
    w.writeFloat(p.shimmerMix);
    w.writeFloat(p.feedback);
    w.writeFloat(p.diffusionAmount);
    w.writeFloat(p.diffusionSize);
    w.writeInt32(p.filterEnabled);
    w.writeFloat(p.filterCutoff);
    w.writeFloat(p.dryWet);
}

void writeTapeState(BinaryWriter& w, const TapePreset& p) {
    // Order MUST match tape_params.h saveTapeParams() - GROUPED, not interleaved!
    w.writeFloat(p.motorSpeed);
    w.writeFloat(p.motorInertia);
    w.writeFloat(p.wear);
    w.writeFloat(p.saturation);
    w.writeFloat(p.age);
    w.writeInt32(p.spliceEnabled);
    w.writeFloat(p.spliceIntensity);
    w.writeFloat(p.feedback);
    w.writeFloat(p.mix);
    // Head enables (grouped)
    w.writeInt32(p.head1Enabled);
    w.writeInt32(p.head2Enabled);
    w.writeInt32(p.head3Enabled);
    // Head levels (grouped)
    w.writeFloat(p.head1Level);
    w.writeFloat(p.head2Level);
    w.writeFloat(p.head3Level);
    // Head pans (grouped)
    w.writeFloat(p.head1Pan);
    w.writeFloat(p.head2Pan);
    w.writeFloat(p.head3Pan);
}

void writeBBDState(BinaryWriter& w, const BBDPreset& p) {
    // Order MUST match bbd_params.h saveBBDParams()
    w.writeFloat(p.delayTime);
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
    w.writeFloat(p.feedback);
    w.writeFloat(p.modulationDepth);
    w.writeFloat(p.modulationRate);
    w.writeFloat(p.age);
    w.writeInt32(p.era);
    w.writeFloat(p.mix);
}

void writeDigitalState(BinaryWriter& w, const DigitalPreset& p) {
    w.writeFloat(p.delayTime);
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
    w.writeFloat(p.feedback);
    w.writeInt32(p.limiterCharacter);
    w.writeInt32(p.era);
    w.writeFloat(p.age);
    w.writeFloat(p.modulationDepth);
    w.writeFloat(p.modulationRate);
    w.writeInt32(p.modulationWaveform);
    w.writeFloat(p.mix);
    w.writeFloat(p.width);
}

void writePingPongState(BinaryWriter& w, const PingPongPreset& p) {
    w.writeFloat(p.delayTime);
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
    w.writeInt32(p.lrRatio);
    w.writeFloat(p.feedback);
    w.writeFloat(p.crossFeedback);
    w.writeFloat(p.width);
    w.writeFloat(p.modulationDepth);
    w.writeFloat(p.modulationRate);
    w.writeFloat(p.mix);
}

void writeReverseState(BinaryWriter& w, const ReversePreset& p) {
    // Order MUST match reverse_params.h saveReverseParams()
    w.writeFloat(p.chunkSize);
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
    w.writeFloat(p.crossfade);
    w.writeInt32(p.playbackMode);
    w.writeFloat(p.feedback);
    w.writeInt32(p.filterEnabled);
    w.writeFloat(p.filterCutoff);
    w.writeInt32(p.filterType);
    w.writeFloat(p.dryWet);
}

void writeMultiTapState(BinaryWriter& w, const MultiTapPreset& p) {
    // Order MUST match multitap_params.h saveMultiTapParams()
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
    w.writeInt32(p.timingPattern);
    w.writeInt32(p.spatialPattern);
    w.writeInt32(p.tapCount);
    w.writeFloat(p.baseTime);
    w.writeFloat(p.tempo);
    w.writeFloat(p.feedback);
    w.writeFloat(p.feedbackLPCutoff);
    w.writeFloat(p.feedbackHPCutoff);
    w.writeFloat(p.morphTime);
    w.writeFloat(p.dryWet);
}

void writeFreezeState(BinaryWriter& w, const FreezePreset& p) {
    // Order MUST match freeze_params.h saveFreezeParams()
    w.writeInt32(p.freezeEnabled);
    w.writeFloat(p.delayTime);
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
    w.writeFloat(p.feedback);
    w.writeFloat(p.pitchSemitones);
    w.writeFloat(p.pitchCents);
    w.writeFloat(p.shimmerMix);
    w.writeFloat(p.decay);
    w.writeFloat(p.diffusionAmount);
    w.writeFloat(p.diffusionSize);
    w.writeInt32(p.filterEnabled);
    w.writeInt32(p.filterType);
    w.writeFloat(p.filterCutoff);
    w.writeFloat(p.dryWet);
}

void writeDuckingState(BinaryWriter& w, const DuckingPreset& p) {
    // Order MUST match ducking_params.h saveDuckingParams()
    w.writeInt32(p.duckingEnabled);
    w.writeFloat(p.threshold);
    w.writeFloat(p.duckAmount);
    w.writeFloat(p.attackTime);
    w.writeFloat(p.releaseTime);
    w.writeFloat(p.holdTime);
    w.writeInt32(p.duckTarget);
    w.writeInt32(p.sidechainFilterEnabled);
    w.writeFloat(p.sidechainFilterCutoff);
    w.writeFloat(p.delayTime);
    w.writeInt32(p.timeMode);
    w.writeInt32(p.noteValue);
    w.writeFloat(p.feedback);
    w.writeFloat(p.dryWet);
}

// Write complete component state matching processor.cpp getState() format
std::vector<uint8_t> buildComponentState(const PresetDef& preset) {
    BinaryWriter w;

    // 1. Global gain (always 1.0 for presets)
    w.writeFloat(1.0f);

    // 2. Current mode
    w.writeInt32(static_cast<int32_t>(preset.mode));

    // 3. All 11 mode parameter packs in order
    // MUST match processor.cpp getState() order exactly!
    // Order is by spec number: 034, 033, 032, 031, 030, 029, 024, 025, 026, 027, 028

    writeGranularState(w, preset.granular);   // spec 034
    writeSpectralState(w, preset.spectral);   // spec 033
    writeDuckingState(w, preset.ducking);     // spec 032
    writeFreezeState(w, preset.freeze);       // spec 031
    writeReverseState(w, preset.reverse);     // spec 030
    writeShimmerState(w, preset.shimmer);     // spec 029
    writeTapeState(w, preset.tape);           // spec 024
    writeBBDState(w, preset.bbd);             // spec 025
    writeDigitalState(w, preset.digital);     // spec 026
    writePingPongState(w, preset.pingpong);   // spec 027
    writeMultiTapState(w, preset.multitap);   // spec 028

    return w.data;
}

// ==============================================================================
// VST3 Preset File Format
// ==============================================================================
// Based on Steinberg VST3 SDK PresetFile implementation.
// Format (from SDK source):
//   Header (48 bytes):
//     - kChunkID "VST3" (4 bytes)
//     - kFormatVersion (4 bytes) = 1
//     - kClassID (32 bytes) = ASCII FUID
//     - listOffset (8 bytes) = offset to chunk list
//   Chunk data section (variable):
//     - Raw chunk data concatenated
//   Chunk list (at listOffset):
//     - Each entry: ChunkID (4) + offset (8) + size (8) = 20 bytes
// ==============================================================================

// Processor FUID from plugin_ids.h: FUID(0x12345678, 0x12345678, 0x12345678, 0x12345678)
// VST3 FUID format stores 4 uint32 values.
// As ASCII hex for preset file, each byte is 2 hex chars = 32 chars total.
// FUID(0x12345678, 0x12345678, 0x12345678, 0x12345678) as raw bytes (big-endian):
//   12345678 12345678 12345678 12345678
// But the actual plugin_ids.h shows placeholder values. The ASCII format is:
//   4 x uint32 printed as 8-char hex each = 32 chars
const char kClassIdAscii[33] = "12345678123456781234567812345678";  // 32 chars + null

void writeLE32(std::ofstream& f, uint32_t val) {
    f.write(reinterpret_cast<const char*>(&val), 4);
}

void writeLE64(std::ofstream& f, int64_t val) {
    f.write(reinterpret_cast<const char*>(&val), 8);
}

bool writeVstPreset(const std::filesystem::path& path,
                    const std::vector<uint8_t>& componentState,
                    const PresetDef& preset) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "Failed to create: " << path << std::endl;
        return false;
    }

    // Layout:
    // [0-3]   "VST3" magic
    // [4-7]   version = 1
    // [8-39]  class ID (32 ASCII chars)
    // [40-47] list offset (int64) -> points to chunk list
    // [48...] component state data
    // [48+N]  chunk list entries

    const int64_t kHeaderSize = 48;  // 4 + 4 + 32 + 8
    const int64_t kChunkEntrySize = 20;  // 4 (id) + 8 (offset) + 8 (size)

    int64_t compDataOffset = kHeaderSize;
    int64_t compDataSize = static_cast<int64_t>(componentState.size());
    int64_t listOffset = compDataOffset + compDataSize;

    // === Write Header ===
    f.write("VST3", 4);              // Magic
    writeLE32(f, 1);                 // Version
    f.write(kClassIdAscii, 32);      // Class ID (32 ASCII hex chars)
    writeLE64(f, listOffset);        // Offset to chunk list

    // === Write Component State Data ===
    f.write(reinterpret_cast<const char*>(componentState.data()), compDataSize);

    // === Write Chunk List ===
    // Format: "List" header + entry count + entries
    // Each entry: ChunkID (4) + offset (8) + size (8)
    f.write("List", 4);              // Chunk list header ID
    writeLE32(f, 1);                 // Entry count = 1
    f.write("Comp", 4);              // Entry 1: Chunk ID (component state)
    writeLE64(f, compDataOffset);    // Entry 1: Data offset
    writeLE64(f, compDataSize);      // Entry 1: Data size

    f.close();
    return true;
}

// ==============================================================================
// Preset Definitions - 10 per mode
// ==============================================================================

std::vector<PresetDef> createAllPresets() {
    std::vector<PresetDef> presets;

    // =========================================================================
    // GRANULAR MODE (0) - Experimental, Vocals, Pads
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Ambient";
        p.name = "Cloud Nine";
        p.granular = {150.0f, 20.0f, 300.0f, 0.0f, 2.0f, 0.3f, 0.5f, 0.1f, 0, 0.4f, 0.5f, 1, 1, 4, 0.2f, 0, 0.7f, 0.8f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Drums";
        p.name = "Stutter Step";
        p.granular = {30.0f, 40.0f, 125.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.2f, 0.5f, 0, 1, 4, 0.0f, 0, 0.3f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Experimental";
        p.name = "Frozen Moment";
        p.granular = {200.0f, 15.0f, 500.0f, 0.0f, 0.0f, 0.5f, 0.3f, 0.0f, 1, 0.8f, 0.5f, 1, 1, 4, 0.1f, 0, 0.8f, 0.6f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Experimental";
        p.name = "Grain Storm";
        p.granular = {50.0f, 50.0f, 200.0f, -12.0f, 12.0f, 0.8f, 0.9f, 0.5f, 0, 0.6f, 0.5f, 2, 1, 4, 0.5f, 0, 0.4f, 1.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Vocals";
        p.name = "Whisper Trail";
        p.granular = {80.0f, 8.0f, 400.0f, 0.0f, 0.5f, 0.1f, 0.2f, 0.0f, 0, 0.3f, 0.5f, 1, 1, 4, 0.05f, 0, 0.6f, 0.4f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Experimental";
        p.name = "Time Warp";
        p.granular = {300.0f, 5.0f, 1000.0f, -24.0f, 0.0f, 0.2f, 0.0f, 0.3f, 0, 0.5f, 0.5f, 1, 1, 4, 0.0f, 0, 0.9f, 0.3f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Rhythmic";
        p.name = "Grain Cascade";
        p.granular = {60.0f, 25.0f, 250.0f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f, 0, 0.45f, 0.5f, 0, 1, 4, 0.0f, 0, 0.5f, 0.7f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Lo-Fi";
        p.name = "Lo-Fi Clouds";
        p.granular = {120.0f, 12.0f, 350.0f, 0.0f, 1.0f, 0.4f, 0.3f, 0.2f, 0, 0.35f, 0.5f, 1, 1, 4, 0.3f, 0, 0.8f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Experimental";
        p.name = "Micro Slice";
        p.granular = {10.0f, 50.0f, 100.0f, 0.0f, 3.0f, 0.0f, 0.4f, 0.0f, 0, 0.1f, 0.5f, 0, 1, 4, 0.0f, 0, 0.2f, 0.6f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Granular; p.category = "Ambient";
        p.name = "Ambient Drift";
        p.granular = {250.0f, 6.0f, 800.0f, 0.0f, 0.5f, 0.6f, 0.4f, 0.15f, 0, 0.55f, 0.5f, 1, 1, 4, 0.15f, 0, 0.85f, 0.7f};
        presets.push_back(p);
    }

    // =========================================================================
    // SPECTRAL MODE (1) - Pads, Experimental, Ambient
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Ambient";
        p.name = "Prism";
        p.spectral = {1024, 300.0f, 500.0f, 0, 0.3f, 0.0f, 0, 0.4f, 0.5f, 0, 0.6f, 1, 4};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Experimental";
        p.name = "Frequency Cascade";
        p.spectral = {2048, 200.0f, 800.0f, 1, 0.4f, 0.3f, 0, 0.3f, 0.5f, 1, 0.5f, 1, 4};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Ambient";
        p.name = "Crystal Diffusion";
        p.spectral = {1024, 400.0f, 300.0f, 2, 0.25f, 0.0f, 0, 0.7f, 0.5f, 0, 0.4f, 1, 4};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Experimental";
        p.name = "Spectral Freeze";
        p.spectral = {4096, 500.0f, 200.0f, 0, 0.6f, 0.0f, 1, 0.5f, 0.5f, 0, 0.3f, 1, 4};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Creative";
        p.name = "Resonant Sweep";
        p.spectral = {1024, 350.0f, 600.0f, 0, 0.7f, 0.5f, 0, 0.2f, 0.5f, 1, 0.5f, 1, 4};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Subtle";
        p.name = "Ghost Frequencies";
        p.spectral = {512, 250.0f, 100.0f, 0, 0.15f, 0.0f, 0, 0.1f, 0.5f, 0, 0.2f, 1, 4};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Experimental";
        p.name = "Rainbow Scatter";
        p.spectral = {2048, 300.0f, 1500.0f, 2, 0.35f, 0.0f, 0, 0.6f, 0.5f, 0, 1.0f, 1, 4};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Stereo";
        p.name = "Mono to Wide";
        p.spectral = {1024, 200.0f, 400.0f, 0, 0.2f, 0.0f, 0, 0.3f, 0.5f, 0, 1.0f, 1, 4};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Bass";
        p.name = "Low End Spread";
        p.spectral = {4096, 400.0f, 300.0f, 0, 0.3f, -0.6f, 0, 0.2f, 0.5f, 1, 0.3f, 1, 4};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Spectral; p.category = "Creative";
        p.name = "Treble Trail";
        p.spectral = {1024, 350.0f, 500.0f, 1, 0.4f, 0.7f, 0, 0.4f, 0.5f, 0, 0.5f, 1, 4};
        presets.push_back(p);
    }

    // =========================================================================
    // SHIMMER MODE (2) - Vocals, Ambient, Guitars
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Ambient";
        p.name = "Heavenly";
        p.shimmer = {800.0f, 12.0f, 0.0f, 1.0f, 0.6f, 0.6f, 50.0f, 0, 4000.0f, 0.5f, 1, 8};  // 1/2
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Dark";
        p.name = "Octave Below";
        p.shimmer = {600.0f, -12.0f, 0.0f, 1.0f, 0.5f, 0.5f, 60.0f, 0, 4000.0f, 0.5f, 1, 7};  // 1/2T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Creative";
        p.name = "Fifth Up";
        p.shimmer = {500.0f, 7.0f, 0.0f, 0.8f, 0.45f, 0.4f, 45.0f, 0, 4000.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Ambient";
        p.name = "Cathedral";
        p.shimmer = {1500.0f, 12.0f, 0.0f, 1.0f, 0.75f, 0.8f, 70.0f, 0, 4000.0f, 0.5f, 1, 9};  // 1/1
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Vocals";
        p.name = "Subtle Shine";
        p.shimmer = {400.0f, 12.0f, 0.0f, 0.4f, 0.3f, 0.3f, 40.0f, 0, 4000.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Dark";
        p.name = "Dark Shimmer";
        p.shimmer = {700.0f, 12.0f, 0.0f, 1.0f, 0.55f, 0.5f, 55.0f, 1, 2000.0f, 0.5f, 1, 7};  // 1/2T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Bright";
        p.name = "Bright Stars";
        p.shimmer = {600.0f, 12.0f, 0.0f, 1.0f, 0.5f, 0.45f, 50.0f, 1, 8000.0f, 0.5f, 1, 7};  // 1/2T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Experimental";
        p.name = "Infinite Rise";
        p.shimmer = {1000.0f, 12.0f, 0.0f, 1.0f, 0.9f, 0.7f, 65.0f, 0, 4000.0f, 0.5f, 1, 8};  // 1/2
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Creative";
        p.name = "Detune Wash";
        p.shimmer = {550.0f, 0.0f, 15.0f, 0.7f, 0.4f, 0.55f, 50.0f, 0, 4000.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Shimmer; p.category = "Vocals";
        p.name = "Vocal Halo";
        p.shimmer = {450.0f, 12.0f, 0.0f, 0.6f, 0.35f, 0.4f, 45.0f, 1, 6000.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }

    // =========================================================================
    // TAPE MODE (3) - Drums, Bass, Vintage
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Vintage";
        p.name = "Worn Cassette";
        p.tape = {0.98f, 0.6f, 0.7f, 0.4f, 0.8f, 0, 0.5f, 0.35f, 0.5f, 1, 1.0f, 0.0f, 0, 0.7f, -0.5f, 0, 0.5f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Clean";
        p.name = "Studio Reel";
        p.tape = {1.0f, 0.3f, 0.1f, 0.2f, 0.1f, 0, 0.5f, 0.4f, 0.5f, 1, 1.0f, 0.0f, 0, 0.7f, -0.5f, 0, 0.5f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Lo-Fi";
        p.name = "VHS Memory";
        p.tape = {0.95f, 0.7f, 0.85f, 0.5f, 0.9f, 1, 0.3f, 0.3f, 0.5f, 1, 1.0f, 0.0f, 0, 0.7f, -0.5f, 0, 0.5f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Classic";
        p.name = "Slapback Echo";
        p.tape = {1.0f, 0.2f, 0.15f, 0.25f, 0.2f, 0, 0.5f, 0.1f, 0.5f, 1, 1.0f, 0.0f, 0, 0.7f, -0.5f, 0, 0.5f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Warm";
        p.name = "Tape Saturation";
        p.tape = {1.0f, 0.4f, 0.3f, 0.7f, 0.4f, 0, 0.5f, 0.45f, 0.5f, 1, 1.0f, 0.0f, 0, 0.7f, -0.5f, 0, 0.5f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Experimental";
        p.name = "Splice Madness";
        p.tape = {0.9f, 0.5f, 0.5f, 0.3f, 0.6f, 1, 0.9f, 0.4f, 0.5f, 1, 1.0f, 0.0f, 1, 0.8f, -0.7f, 1, 0.6f, 0.7f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Dub";
        p.name = "Vintage Dub";
        p.tape = {0.97f, 0.55f, 0.45f, 0.35f, 0.55f, 0, 0.5f, 0.65f, 0.5f, 1, 1.0f, 0.0f, 1, 0.7f, 0.6f, 0, 0.5f, -0.6f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Drums";
        p.name = "Lo-Fi Groove";
        p.tape = {1.0f, 0.35f, 0.4f, 0.45f, 0.5f, 0, 0.5f, 0.25f, 0.5f, 1, 1.0f, 0.0f, 0, 0.7f, -0.5f, 0, 0.5f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Bass";
        p.name = "Warm Bass";
        p.tape = {1.0f, 0.4f, 0.2f, 0.5f, 0.3f, 0, 0.5f, 0.3f, 0.5f, 1, 1.0f, 0.0f, 0, 0.7f, -0.5f, 0, 0.5f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Tape; p.category = "Vintage";
        p.name = "Old Radio";
        p.tape = {0.92f, 0.65f, 0.8f, 0.55f, 0.95f, 1, 0.2f, 0.2f, 0.5f, 1, 1.0f, 0.0f, 0, 0.7f, -0.5f, 0, 0.5f, 0.5f};
        presets.push_back(p);
    }

    // =========================================================================
    // BBD MODE (4) - Guitars, Synths, Vintage
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Classic";
        p.name = "Classic Chorus";
        p.bbd = {20.0f, 0.0f, 0.5f, 0.8f, 0.2f, 1, 0.5f, 1, 0};  // 1/32 (chorus)
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Vintage";
        p.name = "Space Echo";
        p.bbd = {350.0f, 0.5f, 0.3f, 0.4f, 0.5f, 0, 0.5f, 1, 5};  // 1/4T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Warm";
        p.name = "Analog Dreams";
        p.bbd = {280.0f, 0.45f, 0.4f, 0.6f, 0.4f, 1, 0.5f, 1, 4};  // 1/8
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Dark";
        p.name = "Dark Bucket";
        p.bbd = {400.0f, 0.55f, 0.25f, 0.3f, 0.7f, 0, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Creative";
        p.name = "Vintage Flange";
        p.bbd = {8.0f, 0.3f, 0.7f, 0.2f, 0.3f, 0, 0.5f, 1, 0};  // 1/32 (flange)
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Clean";
        p.name = "Clean Repeat";
        p.bbd = {300.0f, 0.4f, 0.1f, 0.5f, 0.1f, 2, 0.5f, 1, 5};  // 1/4T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Lo-Fi";
        p.name = "Murky Depths";
        p.bbd = {450.0f, 0.6f, 0.35f, 0.25f, 0.9f, 0, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Stereo";
        p.name = "Subtle Widen";
        p.bbd = {25.0f, 0.0f, 0.3f, 1.2f, 0.15f, 1, 0.5f, 1, 0};  // 1/32 (widening)
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Drums";
        p.name = "Drum Pocket";
        p.bbd = {120.0f, 0.2f, 0.15f, 0.7f, 0.25f, 1, 0.5f, 1, 2};  // 1/16
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::BBD; p.category = "Synth";
        p.name = "Synth Lead";
        p.bbd = {220.0f, 0.35f, 0.25f, 0.5f, 0.35f, 1, 0.5f, 1, 4};  // 1/8
        presets.push_back(p);
    }

    // =========================================================================
    // DIGITAL MODE (5) - Clean, Precise, Versatile
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Clean";
        p.name = "Crystal Clear";
        p.digital = {500.0f, 1, 4, 0.4f, 1, 2, 0.0f, 0.0f, 1.0f, 0, 0.5f, 100.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Stereo";
        p.name = "Ping Pong Lite";
        p.digital = {375.0f, 1, 4, 0.35f, 1, 2, 0.0f, 0.0f, 1.0f, 0, 0.5f, 150.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Ambient";
        p.name = "Long Tail";
        p.digital = {800.0f, 0, 4, 0.7f, 1, 2, 0.0f, 0.0f, 1.0f, 0, 0.5f, 100.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Rhythmic";
        p.name = "Rhythmic Sync";
        p.digital = {500.0f, 1, 4, 0.45f, 1, 2, 0.0f, 0.0f, 1.0f, 0, 0.5f, 100.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Classic";
        p.name = "Dotted Eighth";
        p.digital = {562.0f, 1, 8, 0.4f, 1, 2, 0.0f, 0.0f, 1.0f, 0, 0.5f, 100.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Subtle";
        p.name = "Subtle Room";
        p.digital = {80.0f, 0, 4, 0.15f, 1, 2, 0.0f, 0.0f, 1.0f, 0, 0.5f, 100.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Creative";
        p.name = "Modulated Space";
        p.digital = {450.0f, 1, 4, 0.5f, 1, 2, 0.0f, 0.3f, 0.8f, 0, 0.5f, 120.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Stereo";
        p.name = "Wide Stereo";
        p.digital = {400.0f, 1, 4, 0.35f, 1, 2, 0.0f, 0.0f, 1.0f, 0, 0.5f, 180.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Vocals";
        p.name = "Clean Vocal";
        p.digital = {350.0f, 1, 4, 0.25f, 1, 2, 0.0f, 0.0f, 1.0f, 0, 0.5f, 100.0f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Digital; p.category = "Drums";
        p.name = "Precise Hit";
        p.digital = {125.0f, 1, 3, 0.2f, 1, 2, 0.0f, 0.0f, 1.0f, 0, 0.5f, 100.0f};
        presets.push_back(p);
    }

    // =========================================================================
    // PINGPONG MODE (6) - Stereo Interest, Guitars, Movement
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Stereo";
        p.name = "Wide Pong";
        p.pingpong = {500.0f, 1, 4, 0, 0.5f, 1.0f, 200.0f, 0.0f, 1.0f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Subtle";
        p.name = "Subtle Bounce";
        p.pingpong = {375.0f, 1, 4, 0, 0.3f, 0.7f, 100.0f, 0.0f, 1.0f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Rhythmic";
        p.name = "Rhythmic Tennis";
        p.pingpong = {250.0f, 1, 3, 0, 0.45f, 1.0f, 150.0f, 0.0f, 1.0f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Ambient";
        p.name = "Slow Motion";
        p.pingpong = {1000.0f, 0, 4, 0, 0.6f, 1.0f, 180.0f, 0.0f, 1.0f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Creative";
        p.name = "Asymmetric";
        p.pingpong = {400.0f, 1, 4, 1, 0.4f, 0.8f, 140.0f, 0.0f, 1.0f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Creative";
        p.name = "Modulated Space";
        p.pingpong = {450.0f, 1, 4, 0, 0.5f, 1.0f, 160.0f, 0.4f, 0.6f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Drums";
        p.name = "Tight Pocket";
        p.pingpong = {125.0f, 1, 3, 0, 0.25f, 1.0f, 120.0f, 0.0f, 1.0f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Guitar";
        p.name = "Guitar Spread";
        p.pingpong = {375.0f, 1, 8, 0, 0.4f, 1.0f, 150.0f, 0.1f, 0.8f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Synth";
        p.name = "Synth Panorama";
        p.pingpong = {333.0f, 1, 4, 2, 0.55f, 1.0f, 180.0f, 0.2f, 0.5f, 0.5f};
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::PingPong; p.category = "Vocals";
        p.name = "Vocal Depth";
        p.pingpong = {300.0f, 1, 4, 0, 0.3f, 0.6f, 80.0f, 0.0f, 1.0f, 0.5f};
        presets.push_back(p);
    }

    // =========================================================================
    // REVERSE MODE (7) - Experimental, Transitions, Ambient
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Ambient";
        p.name = "Ghostly";
        p.reverse = {400.0f, 0.6f, 0, 0.3f, 0, 4000.0f, 0, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Classic";
        p.name = "Backward Glance";
        p.reverse = {500.0f, 0.5f, 0, 0.2f, 0, 4000.0f, 0, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Creative";
        p.name = "Swell Up";
        p.reverse = {800.0f, 0.7f, 0, 0.4f, 0, 4000.0f, 0, 0.5f, 1, 8};  // 1/2
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Experimental";
        p.name = "Alternating Reality";
        p.reverse = {600.0f, 0.55f, 1, 0.35f, 0, 4000.0f, 0, 0.5f, 1, 7};  // 1/2T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Experimental";
        p.name = "Random Chaos";
        p.reverse = {450.0f, 0.45f, 2, 0.25f, 0, 4000.0f, 0, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Dark";
        p.name = "Filtered Ghost";
        p.reverse = {550.0f, 0.55f, 0, 0.4f, 1, 2500.0f, 0, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Rhythmic";
        p.name = "Short Flip";
        p.reverse = {200.0f, 0.4f, 0, 0.15f, 0, 4000.0f, 0, 0.5f, 1, 4};  // 1/8
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Ambient";
        p.name = "Long Tail Reverse";
        p.reverse = {1200.0f, 0.65f, 0, 0.5f, 0, 4000.0f, 0, 0.5f, 1, 9};  // 1/1
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Drums";
        p.name = "Drum Reverse";
        p.reverse = {300.0f, 0.35f, 0, 0.1f, 0, 4000.0f, 0, 0.5f, 1, 5};  // 1/4T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Reverse; p.category = "Vocals";
        p.name = "Vocal Spirit";
        p.reverse = {700.0f, 0.6f, 0, 0.35f, 1, 5000.0f, 0, 0.5f, 1, 7};  // 1/2T
        presets.push_back(p);
    }

    // =========================================================================
    // MULTITAP MODE (8) - Rhythmic Interest, Complex Delays
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Rhythmic";
        p.name = "Cascading Echoes";
        p.multitap = {3, 0, 6, 400.0f, 120.0f, 0.4f, 20000.0f, 20.0f, 500.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Creative";
        p.name = "Golden Ratio";
        p.multitap = {14, 2, 8, 500.0f, 120.0f, 0.45f, 20000.0f, 20.0f, 500.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Creative";
        p.name = "Fibonacci Rhythm";
        p.multitap = {15, 3, 5, 450.0f, 120.0f, 0.5f, 20000.0f, 20.0f, 500.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Stereo";
        p.name = "Wide Taps";
        p.multitap = {2, 1, 4, 375.0f, 120.0f, 0.35f, 20000.0f, 20.0f, 500.0f, 0.5f, 1, 5};  // 1/4T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Drums";
        p.name = "Tight Pocket";
        p.multitap = {4, 2, 3, 125.0f, 120.0f, 0.2f, 15000.0f, 100.0f, 300.0f, 0.5f, 1, 2};  // 1/16
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Dub";
        p.name = "Dub Echoes";
        p.multitap = {2, 0, 4, 500.0f, 120.0f, 0.6f, 8000.0f, 80.0f, 600.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Rhythmic";
        p.name = "Sixteenth Grid";
        p.multitap = {4, 2, 8, 125.0f, 120.0f, 0.3f, 20000.0f, 20.0f, 400.0f, 0.5f, 1, 2};  // 1/16
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Ambient";
        p.name = "Slow Buildup";
        p.multitap = {16, 4, 12, 800.0f, 80.0f, 0.55f, 12000.0f, 40.0f, 800.0f, 0.5f, 1, 8};  // 1/2
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Stereo";
        p.name = "Alternating Stereo";
        p.multitap = {3, 1, 6, 333.0f, 120.0f, 0.4f, 20000.0f, 20.0f, 500.0f, 0.5f, 1, 5};  // 1/4T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::MultiTap; p.category = "Experimental";
        p.name = "Prime Numbers";
        p.multitap = {17, 2, 7, 400.0f, 120.0f, 0.45f, 20000.0f, 20.0f, 500.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }

    // =========================================================================
    // FREEZE MODE (9) - Drones, Textures, Sustain
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Sustain";
        p.name = "Infinite Sustain";
        p.freeze = {0, 500.0f, 0.95f, 0.0f, 0.0f, 0.0f, 0.9f, 0.3f, 0.5f, 0, 0, 1000.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Ambient";
        p.name = "Drone Bed";
        p.freeze = {0, 800.0f, 0.9f, 0.0f, 0.0f, 0.0f, 0.95f, 0.5f, 0.6f, 0, 0, 1000.0f, 0.5f, 1, 8};  // 1/2
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Shimmer";
        p.name = "Shimmer Freeze";
        p.freeze = {0, 600.0f, 0.85f, 12.0f, 0.0f, 0.6f, 0.8f, 0.4f, 0.5f, 0, 0, 1000.0f, 0.5f, 1, 7};  // 1/2T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Dark";
        p.name = "Dark Frozen";
        p.freeze = {0, 700.0f, 0.9f, 0.0f, 0.0f, 0.0f, 0.85f, 0.4f, 0.55f, 1, 0, 800.0f, 0.5f, 1, 7};  // 1/2T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Bright";
        p.name = "Bright Freeze";
        p.freeze = {0, 500.0f, 0.88f, 0.0f, 0.0f, 0.0f, 0.82f, 0.35f, 0.5f, 1, 1, 2000.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Creative";
        p.name = "Evolving Texture";
        p.freeze = {0, 650.0f, 0.8f, 7.0f, 10.0f, 0.3f, 0.75f, 0.5f, 0.6f, 0, 0, 1000.0f, 0.5f, 1, 7};  // 1/2T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Synth";
        p.name = "Pad Machine";
        p.freeze = {0, 550.0f, 0.92f, 0.0f, 0.0f, 0.2f, 0.88f, 0.45f, 0.55f, 0, 0, 1000.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Vocals";
        p.name = "Vocal Hold";
        p.freeze = {0, 400.0f, 0.85f, 0.0f, 0.0f, 0.0f, 0.8f, 0.3f, 0.45f, 0, 0, 1000.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Guitar";
        p.name = "Guitar Sustainer";
        p.freeze = {0, 450.0f, 0.9f, 0.0f, 0.0f, 0.15f, 0.85f, 0.35f, 0.5f, 0, 0, 1000.0f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Freeze; p.category = "Ambient";
        p.name = "Ambient Layer";
        p.freeze = {0, 750.0f, 0.88f, 0.0f, 0.0f, 0.1f, 0.9f, 0.5f, 0.6f, 0, 0, 1000.0f, 0.5f, 1, 8};  // 1/2
        presets.push_back(p);
    }

    // =========================================================================
    // DUCKING MODE (10) - Mix Clarity, Vocals, Professional
    // =========================================================================
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Vocals";
        p.name = "Vocal Space";
        p.ducking = {1, -24.0f, 0.7f, 5.0f, 150.0f, 30.0f, 0, 0, 80.0f, 400.0f, 0.35f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Drums";
        p.name = "Drum Clarity";
        p.ducking = {1, -18.0f, 0.8f, 2.0f, 100.0f, 20.0f, 0, 0, 80.0f, 250.0f, 0.25f, 0.5f, 1, 4};  // 1/8
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Creative";
        p.name = "Sidechain Pump";
        p.ducking = {1, -20.0f, 0.9f, 1.0f, 250.0f, 50.0f, 0, 0, 80.0f, 500.0f, 0.5f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Subtle";
        p.name = "Subtle Duck";
        p.ducking = {1, -30.0f, 0.4f, 10.0f, 200.0f, 40.0f, 0, 0, 80.0f, 350.0f, 0.3f, 0.5f, 1, 5};  // 1/4T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Fast";
        p.name = "Fast Response";
        p.ducking = {1, -22.0f, 0.65f, 0.5f, 80.0f, 10.0f, 0, 0, 80.0f, 300.0f, 0.35f, 0.5f, 1, 5};  // 1/4T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Slow";
        p.name = "Slow Pump";
        p.ducking = {1, -26.0f, 0.75f, 20.0f, 400.0f, 100.0f, 0, 0, 80.0f, 600.0f, 0.45f, 0.5f, 1, 7};  // 1/2T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Bass";
        p.name = "Bass Focus";
        p.ducking = {1, -24.0f, 0.7f, 8.0f, 180.0f, 50.0f, 0, 1, 150.0f, 450.0f, 0.4f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Clean";
        p.name = "Clean Pass";
        p.ducking = {1, -20.0f, 0.85f, 3.0f, 120.0f, 25.0f, 0, 0, 80.0f, 375.0f, 0.3f, 0.5f, 1, 5};  // 1/4T
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Creative";
        p.name = "Echo Breath";
        p.ducking = {1, -28.0f, 0.6f, 15.0f, 300.0f, 80.0f, 2, 0, 80.0f, 500.0f, 0.5f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }
    {
        PresetDef p; p.mode = DelayMode::Ducking; p.category = "Mix";
        p.name = "Mix Glue";
        p.ducking = {1, -32.0f, 0.35f, 12.0f, 220.0f, 60.0f, 0, 0, 80.0f, 400.0f, 0.35f, 0.5f, 1, 6};  // 1/4
        presets.push_back(p);
    }

    return presets;
}

// ==============================================================================
// Main
// ==============================================================================

int main(int argc, char* argv[]) {
    std::filesystem::path outputDir = "resources/presets";

    if (argc > 1) {
        outputDir = argv[1];
    }

    // Create output directory
    std::filesystem::create_directories(outputDir);

    auto presets = createAllPresets();
    int successCount = 0;

    std::cout << "Generating " << presets.size() << " factory presets..." << std::endl;

    for (const auto& preset : presets) {
        // Build component state
        auto state = buildComponentState(preset);

        // Mode subdirectory names (must match resources/presets/ structure)
        const char* modeNames[] = {
            "Granular", "Spectral", "Shimmer", "Tape", "BBD",
            "Digital", "PingPong", "Reverse", "MultiTap", "Freeze", "Ducking"
        };

        // Create mode subdirectory path
        auto modeDir = outputDir / modeNames[static_cast<int>(preset.mode)];
        std::filesystem::create_directories(modeDir);

        // Create filename: PresetName.vstpreset (clean name, no mode prefix)
        std::string filename;
        for (char c : preset.name) {
            if (c == ' ') filename += '_';
            else if (std::isalnum(c)) filename += c;
        }
        filename += ".vstpreset";

        auto path = modeDir / filename;

        if (writeVstPreset(path, state, preset)) {
            std::cout << "  Created: " << modeNames[static_cast<int>(preset.mode)] << "/" << filename << std::endl;
            successCount++;
        }
    }

    std::cout << "\nGenerated " << successCount << " of " << presets.size() << " presets." << std::endl;
    std::cout << "Output directory: " << std::filesystem::absolute(outputDir) << std::endl;

    return (successCount == static_cast<int>(presets.size())) ? 0 : 1;
}
