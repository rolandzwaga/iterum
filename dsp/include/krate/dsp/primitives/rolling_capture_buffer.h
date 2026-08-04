// ==============================================================================
// Layer 1: DSP Primitive - Rolling Capture Buffer
// ==============================================================================
// Continuously recording stereo circular buffer for Pattern Freeze Mode.
//
// Maintains a rolling capture of the most recent audio, allowing slices to be
// extracted at any position for playback in freeze patterns. Optimized for
// real-time operation with no allocations during write/read operations.
//
// Constitution Compliance:
// - Principle II: Real-Time Safety (noexcept on write/read, no RT allocations)
// - Principle III: Modern C++ (RAII, value semantics, C++20)
// - Principle IX: Layer 1 (depends only on Layer 0 / standard library)
// - Principle XII: Test-First Development
//
// Reference: specs/069-pattern-freeze/data-model.md
// ==============================================================================

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Krate::DSP {

/// @brief Continuously recording stereo circular buffer for pattern freeze
///
/// Records incoming audio in a circular buffer, allowing slices to be extracted
/// from any position for use in freeze pattern playback. The buffer maintains
/// the most recent N seconds of audio.
///
/// @note prepare() allocates memory; write/read operations are allocation-free.
/// @note All read/write methods are noexcept for real-time safety.
///
/// @example
/// @code
/// RollingCaptureBuffer buffer;
/// buffer.prepare(44100.0, 2.0f);  // 2 seconds at 44.1kHz
///
/// // In audio callback:
/// buffer.writeStereo(inputL, inputR);
///
/// // When freeze triggers:
/// if (buffer.isReady(500.0f)) {
///     buffer.extractSlice(sliceL, sliceR, sliceLength, offsetSamples);
/// }
/// @endcode
class RollingCaptureBuffer {
public:
    /// @brief Default constructor - creates uninitialized buffer
    RollingCaptureBuffer() noexcept = default;

    /// @brief Destructor
    ~RollingCaptureBuffer() = default;

    // Non-copyable, movable
    RollingCaptureBuffer(const RollingCaptureBuffer&) = delete;
    RollingCaptureBuffer& operator=(const RollingCaptureBuffer&) = delete;
    RollingCaptureBuffer(RollingCaptureBuffer&&) noexcept = default;
    RollingCaptureBuffer& operator=(RollingCaptureBuffer&&) noexcept = default;

    // =========================================================================
    // Lifecycle Methods
    // =========================================================================

    /// @brief Prepare buffer for recording
    ///
    /// Allocates circular buffer sized for the specified duration.
    /// Buffer size is rounded up to next power of 2 for efficient wraparound.
    ///
    /// @param sampleRate Sample rate in Hz
    /// @param maxDurationSeconds Maximum recording duration in seconds
    void prepare(double sampleRate, float maxDurationSeconds) noexcept {
        sampleRate_ = sampleRate;

        // Calculate required capacity
        const size_t requiredSamples = static_cast<size_t>(
            sampleRate * static_cast<double>(maxDurationSeconds));

        // Round up to next power of 2 for efficient wraparound
        capacity_ = nextPowerOf2(requiredSamples);

        // Allocate stereo buffers
        bufferL_.resize(capacity_, 0.0f);
        bufferR_.resize(capacity_, 0.0f);

        // Calculate bitmask for efficient modulo
        mask_ = capacity_ - 1;

        reset();
    }

    /// @brief Reset buffer state (clear content, reset write position)
    void reset() noexcept {
        std::fill(bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill(bufferR_.begin(), bufferR_.end(), 0.0f);
        writeIndex_ = 0;
        samplesWritten_ = 0;
    }

    // =========================================================================
    // Recording (Real-Time Safe)
    // =========================================================================

    /// @brief Write a stereo sample to the buffer
    ///
    /// @param left Left channel sample
    /// @param right Right channel sample
    ///
    /// @note O(1) time, no allocations, noexcept - safe for audio thread
    void writeStereo(float left, float right) noexcept {
        bufferL_[writeIndex_] = left;
        bufferR_[writeIndex_] = right;

        writeIndex_ = (writeIndex_ + 1) & mask_;

        if (samplesWritten_ < capacity_) {
            ++samplesWritten_;
        }
    }

    // =========================================================================
    // Slice Extraction (Real-Time Safe)
    // =========================================================================

    /// @brief Extract a slice of audio from the buffer
    ///
    /// Copies a contiguous slice of audio from the circular buffer to the
    /// output arrays. The slice starts at the specified offset before the
    /// current write position.
    ///
    /// @param outLeft Output buffer for left channel (must have lengthSamples capacity)
    /// @param outRight Output buffer for right channel (must have lengthSamples capacity)
    /// @param lengthSamples Number of samples to extract
    /// @param offsetSamples Start position as samples before current write position
    ///
    /// @note Offset 0 means start at most recent sample
    /// @note If requested range exceeds available data, silently clamps
    void extractSlice(float* outLeft, float* outRight, size_t lengthSamples,
                      size_t offsetSamples) const noexcept {
        if (lengthSamples == 0 || outLeft == nullptr || outRight == nullptr) {
            return;
        }

        // Clamp length to available data
        const size_t available = getAvailableSamples();
        lengthSamples = std::min(lengthSamples, available);

        if (lengthSamples == 0) {
            return;
        }

        // Clamp offset to available range
        offsetSamples = std::min(offsetSamples, available - lengthSamples);

        // Calculate start read position
        // writeIndex_ points to next write location, so most recent sample is at writeIndex_ - 1
        // offset N means go back N more samples from most recent
        const size_t startOffset = offsetSamples + lengthSamples;
        const size_t startIndex = (writeIndex_ - startOffset + capacity_) & mask_;

        // Copy samples, handling wraparound
        for (size_t i = 0; i < lengthSamples; ++i) {
            const size_t readIdx = (startIndex + i) & mask_;
            outLeft[i] = bufferL_[readIdx];
            outRight[i] = bufferR_[readIdx];
        }
    }

    // =========================================================================
    // Interpolated age-domain reading (Real-Time Safe)
    // =========================================================================

    /// @brief A snapshot of the ring's addressing state, for many interpolated
    ///        reads taken at ONE write position.
    ///
    /// WHY THIS EXISTS. readStereoLinear() below re-derives six loop-invariant
    /// quantities on every call - the available-sample count, its float
    /// `maxAge`, the write index, the capacity, the mask and both data pointers
    /// - and only then does eight bytes of useful work. A granular reader calls
    /// it up to twice per grain per output sample, so at a 64-grain population
    /// that setup runs 128 times per sample against ONE write. Snapshotting it
    /// once per output sample and reusing it for the whole grain sweep is the
    /// same arithmetic with the invariants hoisted.
    ///
    /// LIFETIME. The snapshot is valid only until the next writeStereo(),
    /// reset() or prepare() on the buffer it came from: it caches the write
    /// position and the raw storage pointers. Take it AFTER the write for the
    /// current sample and discard it before the next one. It is a view, not an
    /// owner, and it is trivially copyable by design.
    ///
    /// The interpolation, the clamp and the fast-math contract are IDENTICAL to
    /// readStereoLinear(), because readStereoLinear() is implemented in terms of
    /// this - there is one copy of that arithmetic, not two.
    class LinearReader {
    public:
        /// @brief Read one interpolated stereo sample at `ageSamples`.
        void readStereo(float ageSamples, float& outLeft, float& outRight) const noexcept {
            if (!valid_) {
                outLeft = 0.0f;
                outRight = 0.0f;
                return;
            }
            const size_t i0 = index0(ageSamples);
            const size_t i1 = (i0 + mask_) & mask_;  // one sample OLDER
            const float frac = frac_;
            outLeft = left_[i0] + frac * (left_[i1] - left_[i0]);
            outRight = right_[i0] + frac * (right_[i1] - right_[i0]);
        }

        /// @brief Read ONLY the right channel at `ageSamples`.
        ///
        /// A decorrelating reader takes L at one age and R at another, so the
        /// second read's left half is loaded, interpolated and thrown away. This
        /// halves that read.
        [[nodiscard]] float readRight(float ageSamples) const noexcept {
            if (!valid_) {
                return 0.0f;
            }
            const size_t i0 = index0(ageSamples);
            const size_t i1 = (i0 + mask_) & mask_;
            return right_[i0] + frac_ * (right_[i1] - right_[i0]);
        }

        /// @brief Read at `ageSamples` relative to a write head `newerOffset`
        ///        samples BEFORE this snapshot's.
        ///
        /// Exists for AtmosphereEngine's chunk-hoisted grain sweep (Phase
        /// 11.5): one snapshot serves a whole 64-sample chunk, but each output
        /// sample's age must stay relative to THAT SAMPLE's write head - the
        /// former one-snapshot-per-sample identity - or the float quantization
        /// of a large age becomes a function of where the chunk boundary fell
        /// and the render is no longer partition-invariant (the measured
        /// failure mode: 3.3e-4 against a 1e-5 partition bound). Rebasing the
        /// INDEX by the integer offset while the caller forms the age against
        /// the per-sample head keeps position and interpolation weights
        /// bit-identical to a per-sample snapshot.
        ///
        /// @pre newerOffset < the number of valid samples this snapshot covers
        ///      (the atmosphere caller passes numSamples - 1 - i, <= 63).
        void readStereoOffset(float ageSamples, size_t newerOffset, float& outLeft,
                              float& outRight) const noexcept {
            if (!valid_) {
                outLeft = 0.0f;
                outRight = 0.0f;
                return;
            }
            const size_t i0 = (index0(ageSamples) - newerOffset) & mask_;
            const size_t i1 = (i0 + mask_) & mask_;  // one sample OLDER
            const float frac = frac_;
            outLeft = left_[i0] + frac * (left_[i1] - left_[i0]);
            outRight = right_[i0] + frac * (right_[i1] - right_[i0]);
        }

        /// @brief readRight(), rebased like readStereoOffset().
        [[nodiscard]] float readRightOffset(float ageSamples, size_t newerOffset) const noexcept {
            if (!valid_) {
                return 0.0f;
            }
            const size_t i0 = (index0(ageSamples) - newerOffset) & mask_;
            const size_t i1 = (i0 + mask_) & mask_;
            return right_[i0] + frac_ * (right_[i1] - right_[i0]);
        }

        /// @brief The INDEXING HALF of readStereoOffset(), without the loads:
        ///        ring indices and interpolation weight for a rebased read.
        ///
        /// Exists for the gather-based grain span kernel (Phase 11.5): a
        /// scalar pass precomputes every sample's indices/weight with EXACTLY
        /// this arithmetic - the same clamp, truncation and rebase, so
        /// positions are bit-identical to readStereoOffset() - and a SIMD pass
        /// then gathers and interpolates. i1 is one sample OLDER than i0, the
        /// readStereo() identity. Capacity < 2^31 always (the ring is at most
        /// ~2^22 frames), so int32 indices are exact.
        ///
        /// @pre isValid(); the caller takes the zero-yield path itself when not.
        void indexAt(float ageSamples, size_t newerOffset, std::int32_t& outI0,
                     std::int32_t& outI1, float& outFrac) const noexcept {
            const size_t i0 = (index0(ageSamples) - newerOffset) & mask_;
            outI0 = static_cast<std::int32_t>(i0);
            outI1 = static_cast<std::int32_t>((i0 + mask_) & mask_);
            outFrac = frac_;
        }

        /// Raw channel storage for the gather kernel. Never null while
        /// isValid(); lifetime is the snapshot's (see the class banner).
        [[nodiscard]] const float* leftData() const noexcept { return left_; }
        [[nodiscard]] const float* rightData() const noexcept { return right_; }

        /// @brief False when the source buffer held fewer than 2 samples, or was
        ///        not prepared. Every read then yields 0.
        [[nodiscard]] bool isValid() const noexcept { return valid_; }

    private:
        friend class RollingCaptureBuffer;

        /// The clamp, the floor and the fraction - see readStereoLinear()'s
        /// fast-math note for why the clamp is two ordered comparisons.
        ///
        /// TRUNCATION, NOT std::floor, AND THAT IS WORTH A PARAGRAPH. After the
        /// two comparisons above `age` is in [0, maxAge_], and for a
        /// non-negative value truncation-toward-zero IS the floor - the two are
        /// bit-identical, not merely close. The distinction is a codegen one:
        /// x86-64's `roundss` needs SSE4.1, which MSVC does not target by
        /// default, so `std::floor(float)` compiles to a CALL to the CRT's
        /// floorf on the default /arch. On AtmosphereEngine's innermost loop
        /// that is up to two library calls per grain per output sample, and it
        /// MEASURED as roughly half the whole per-grain-sample cost. The
        /// truncation below is one vcvttss2si, and the integer it produces is
        /// the one the indexing needs anyway.
        [[nodiscard]] size_t index0(float ageSamples) const noexcept {
            float age = ageSamples;
            if (!(age >= 0.0f)) { age = 0.0f; }    // negatives, -Inf, NaN
            if (age > maxAge_)  { age = maxAge_; }  // +Inf, over-range finite
            const auto ageInt = static_cast<std::int64_t>(age);
            frac_ = age - static_cast<float>(ageInt);
            return (base_ - static_cast<size_t>(ageInt)) & mask_;
        }

        const float* left_ = nullptr;
        const float* right_ = nullptr;
        size_t base_ = 0;   ///< writeIndex_ + capacity_ - 1, i.e. the age-0 index
        size_t mask_ = 0;   ///< capacity_ - 1; also the "one sample older" step
        float maxAge_ = 0.0f;
        mutable float frac_ = 0.0f;  ///< set by index0(), consumed by the caller
        bool valid_ = false;
    };

    /// @brief Snapshot the ring's addressing state for interpolated reads.
    ///
    /// @note O(1), const, noexcept. See LinearReader for the lifetime rule.
    [[nodiscard]] LinearReader makeLinearReader() const noexcept {
        LinearReader r;
        const size_t available = getAvailableSamples();
        if (capacity_ == 0 || available < 2) {
            return r;  // valid_ == false; every read yields 0
        }
        r.left_ = bufferL_.data();
        r.right_ = bufferR_.data();
        r.base_ = writeIndex_ + capacity_ - 1;
        r.mask_ = mask_;
        // Signed intermediate: x86-64 has no unsigned-64 -> float instruction
        // below AVX-512, so the unsigned form expands to a compare, a branch and
        // a fix-up. `available - 2` is non-negative here and bounded by the
        // capacity, so the conversion is value-identical.
        r.maxAge_ = static_cast<float>(static_cast<std::int64_t>(available - 2));
        r.valid_ = true;
        return r;
    }

    /// @brief Read one linearly-interpolated stereo sample at a fractional age.
    ///
    /// Age 0.0 addresses the MOST RECENT written sample (index `writeIndex_ - 1`);
    /// increasing age moves back in time. The value is interpolated between the
    /// samples at ages `floor(age)` and `floor(age) + 1`.
    ///
    /// @par Relationship to extractSlice(). extractSlice anchors from the END of
    ///      the slice (`startOffset = offsetSamples + lengthSamples`, :161-162), so
    ///      `out[i]` has age `offsetSamples + lengthSamples - 1 - i`. The two agree
    ///      on "same offset" ONLY at `lengthSamples == 1`. The general identity is
    ///          extractSlice(outL, outR, L, O)[i] == readStereoLinear(O + L - 1 - i)
    ///
    /// @param ageSamples  Age behind the write head; clamped to
    ///                    [0, getAvailableSamples() - 2]. A NON-FINITE argument
    ///                    lands on age 0 (the most recent sample) via the same
    ///                    two ordered comparisons that perform the clamp -- see
    ///                    the fast-math note below.
    /// @param outLeft     Receives the interpolated left sample.
    /// @param outRight    Receives the interpolated right sample.
    ///
    /// @note Unprepared buffer, or fewer than 2 samples written, yields (0, 0) and
    ///       touches nothing. Both halves of that guard are load-bearing: before
    ///       prepare() `capacity_ = mask_ = 0` and both vectors are EMPTY (:227-230),
    ///       so `bufferL_[idx & mask_]` would be an out-of-bounds read; and
    ///       getAvailableSamples() is size_t starting at 0 (:204, :100), so a bare
    ///       `available - 2` wraps to ~2^64 and the clamp becomes a no-op in exactly
    ///       the case it exists for.
    ///
    /// @note NON-FINITE ARGUMENTS AND -ffast-math. This function is a header that
    ///       lands in translation units built with /fp:fast (MSVC) and -ffast-math
    ///       (the macOS leg, via the VST3 SDK's global flags). `core/db_utils.h:44-52`
    ///       states the resulting contract verbatim: "Source files using this
    ///       function MUST be compiled with -fno-fast-math", and the repo's existing
    ///       remedy for headers that cannot guarantee that is to hide the check
    ///       behind a call boundary (`ITERUM_NOINLINE`, `primitives/smoother.h:37-45`
    ///       -- "Required to prevent branch elimination with NaN checks under
    ///       /fp:fast", applied at `:170`, `:342`, `:519`). NEITHER is used here,
    ///       because this function sits on AtmosphereEngine's innermost loop (up to
    ///       2 calls per grain per sample) where a non-inlinable call per invocation
    ///       is unaffordable. Instead the guard is expressed as TWO ORDINARY ORDERED
    ///       COMPARISONS which double as the range clamp: `!(age >= 0)` is taken by
    ///       negatives, by -Inf and by NaN (an unordered compare yields false), and
    ///       `age > maxAge` is then taken by +Inf. Neither is an FP classification
    ///       predicate, so -ffinite-math-only has nothing to fold away -- it cannot
    ///       prove `age >= 0.0f`. The result is always a finite value in
    ///       [0, maxAge] with no bit test, no call boundary and no added cost.
    ///
    /// @note O(1), const, noexcept, allocation-free. Steady-state indexing uses the
    ///       existing `& mask_` wraparound (:117, :166) -- no new indexing scheme and
    ///       no per-wrap branch.
    void readStereoLinear(float ageSamples, float& outLeft,
                          float& outRight) const noexcept {
        // ONE implementation of the clamp, the indexing and the interpolation -
        // LinearReader's. The FR-081 guard is makeLinearReader()'s `valid_`
        // flag, and an invalid reader yields (0, 0) exactly as this function
        // always has.
        makeLinearReader().readStereo(ageSamples, outLeft, outRight);
    }

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// @brief Check if buffer has enough data for the specified duration
    ///
    /// @param minDurationMs Minimum required recording duration in milliseconds
    /// @return true if at least minDurationMs of audio has been recorded
    [[nodiscard]] bool isReady(float minDurationMs) const noexcept {
        const size_t requiredSamples = static_cast<size_t>(
            sampleRate_ * static_cast<double>(minDurationMs) / 1000.0);
        return samplesWritten_ >= requiredSamples;
    }

    /// @brief Get buffer capacity in samples
    [[nodiscard]] size_t getCapacitySamples() const noexcept {
        return capacity_;
    }

    /// @brief Get sample rate
    [[nodiscard]] double getSampleRate() const noexcept {
        return sampleRate_;
    }

    /// @brief Get number of samples written since prepare/reset
    [[nodiscard]] size_t getSamplesWritten() const noexcept {
        return samplesWritten_;
    }

    /// @brief Get number of samples available for extraction
    ///
    /// Returns the lesser of samples written or buffer capacity.
    [[nodiscard]] size_t getAvailableSamples() const noexcept {
        return std::min(samplesWritten_, capacity_);
    }

private:
    /// @brief Compute next power of 2 >= n
    [[nodiscard]] static constexpr size_t nextPowerOf2(size_t n) noexcept {
        if (n == 0) return 1;
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    // Buffer storage
    std::vector<float> bufferL_;    ///< Left channel circular buffer
    std::vector<float> bufferR_;    ///< Right channel circular buffer

    // Buffer state
    size_t capacity_ = 0;           ///< Buffer capacity (power of 2)
    size_t mask_ = 0;               ///< Bitmask for efficient wraparound
    size_t writeIndex_ = 0;         ///< Current write position
    size_t samplesWritten_ = 0;     ///< Total samples written (capped at capacity)

    // Configuration
    double sampleRate_ = 44100.0;   ///< Sample rate in Hz
};

}  // namespace Krate::DSP
