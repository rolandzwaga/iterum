// ==============================================================================
// render_fingerprint.h -- portable "this render is unchanged" regression pin
// ==============================================================================
// Pinning a DSP render by hashing its raw float bits (FNV over the sample
// bytes) does NOT work across toolchains. MSVC, GCC and Apple Clang differ in
// the last bits of every transcendental, and macOS CI additionally builds with
// -ffast-math. A single 1-ULP difference anywhere in the render changes the
// digest completely, so an MSVC-generated digest is guaranteed red on the Linux
// and macOS legs -- which is exactly how the phaser and flanger goldens broke.
//
// A fingerprint compares what a regression would actually move:
//
//   * four aggregate metrics over every sample -- RMS, peak, mean absolute
//     value, and total variation (sum of |x[i] - x[i-1]|). Total variation is
//     the sharp one: it tracks waveform shape, so a changed filter coefficient
//     or feedback path moves it even when RMS happens to land in the same place.
//   * evenly spaced sample checkpoints, which catch a localised change that the
//     aggregates could average away.
//
// The tolerances below are measured, not guessed. Rendering the phaser and
// flanger cases under g++ -O3, g++ -O3 -ffast-math and clang++ -O2 and diffing
// sample by sample gave:
//
//   worst per-sample absolute difference   2.9e-5   (against a signal peak of 2.17)
//   worst aggregate relative difference    1.9e-7
//
// so kSampleTolerance is set ~3x above the measured sample spread and
// kMetricTolerance ~50x above the measured metric spread. Both remain far
// tighter than any real DSP change: swapping std::tanh for a Pade approximant
// moves these metrics by parts in 1e-3 or more, which these bounds reject.
//
// 2026-08-05/06 re-measurements — the real cross-toolchain spread, from CI and
// local builds (previously green bounds were 1e-5 metric / 1e-4 sample):
//   Apple Clang (Xcode 26.6, -ffast-math):        metric 2.0041e-5, sample 8.96e-5
//   g++ 13 -ffast-math + the guard barrier
//     (trajectory-bearing morph cells, mut=1):    metric 9.36659e-5, sample 3.73498e-4
// Bounds re-derived from the worst of those with ~2.5x headroom. Still well
// below any real DSP change (parts in 1e-3+; the injected stale-sweep-cache
// bug moves samples by 0.38 against these bounds).
// ==============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>

namespace Krate {
namespace DSP {
namespace TestUtils {

/// Number of evenly spaced sample checkpoints stored per render.
inline constexpr std::size_t kRenderCheckpoints = 32;

/// Largest absolute per-sample difference treated as toolchain noise.
inline constexpr float kSampleTolerance = 5.0e-4f;

/// Largest relative aggregate-metric difference treated as toolchain noise.
inline constexpr double kMetricTolerance = 2.5e-4;

struct RenderFingerprint {
    double rms = 0.0;
    double peak = 0.0;
    double meanAbs = 0.0;
    double totalVariation = 0.0;
    std::array<float, kRenderCheckpoints> checkpoints{};
};

/// Reduce a rendered buffer to its fingerprint. Accumulates in double so the
/// aggregate metrics are not themselves a source of cross-toolchain spread.
[[nodiscard]] inline RenderFingerprint fingerprintRender(std::span<const float> samples) {
    RenderFingerprint fp;
    if (samples.empty()) return fp;

    double sumSquares = 0.0;
    double sumAbs = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double s = static_cast<double>(samples[i]);
        sumSquares += s * s;
        sumAbs += std::abs(s);
        fp.peak = std::max(fp.peak, std::abs(s));
        if (i > 0) {
            fp.totalVariation += std::abs(s - static_cast<double>(samples[i - 1]));
        }
    }
    const double n = static_cast<double>(samples.size());
    fp.rms = std::sqrt(sumSquares / n);
    fp.meanAbs = sumAbs / n;

    const std::size_t stride = std::max<std::size_t>(1, samples.size() / kRenderCheckpoints);
    for (std::size_t k = 0; k < kRenderCheckpoints; ++k) {
        fp.checkpoints[k] = samples[std::min(k * stride, samples.size() - 1)];
    }
    return fp;
}

struct FingerprintComparison {
    double worstMetricRelativeError = 0.0;
    float worstSampleError = 0.0f;
    /// The bounds this comparison was made against (the shared constants unless
    /// the caller passed measured per-comparison overrides).
    double metricTolerance = kMetricTolerance;
    float sampleTolerance = kSampleTolerance;
    std::string detail;

    [[nodiscard]] bool withinTolerance() const {
        return worstMetricRelativeError <= metricTolerance && worstSampleError <= sampleTolerance;
    }
};

/// Compare a freshly rendered fingerprint against a pinned reference.
///
/// The defaulted tolerances are the shared cross-toolchain constants above. A
/// caller comparing against a STORED golden of a trajectory-accumulating
/// render (drift, mutation, chaotic modulators) may pass a looser MEASURED
/// sample bound for that comparison only — checkpoint samples of such renders
/// land on different trajectory phases under a legal codegen change, while the
/// aggregate metrics stay sharp. Never loosen the shared constants for one
/// caller's sake.
[[nodiscard]] inline FingerprintComparison compareFingerprints(
    const RenderFingerprint& actual, const RenderFingerprint& reference,
    double metricTolerance = kMetricTolerance, float sampleTolerance = kSampleTolerance) {
    FingerprintComparison out;
    out.metricTolerance = metricTolerance;
    out.sampleTolerance = sampleTolerance;

    const auto metric = [&out](const char* name, double a, double b) {
        const double denom = std::max(std::abs(b), 1.0e-12);
        const double rel = std::abs(a - b) / denom;
        if (rel > out.worstMetricRelativeError) {
            out.worstMetricRelativeError = rel;
            out.detail = std::string(name) + " actual=" + std::to_string(a) +
                         " reference=" + std::to_string(b);
        }
    };
    metric("rms", actual.rms, reference.rms);
    metric("peak", actual.peak, reference.peak);
    metric("meanAbs", actual.meanAbs, reference.meanAbs);
    metric("totalVariation", actual.totalVariation, reference.totalVariation);

    for (std::size_t k = 0; k < kRenderCheckpoints; ++k) {
        const float err = std::abs(actual.checkpoints[k] - reference.checkpoints[k]);
        if (err > out.worstSampleError) {
            out.worstSampleError = err;
            if (err > out.sampleTolerance) {
                out.detail = "checkpoint[" + std::to_string(k) +
                             "] actual=" + std::to_string(actual.checkpoints[k]) +
                             " reference=" + std::to_string(reference.checkpoints[k]);
            }
        }
    }
    return out;
}

} // namespace TestUtils
} // namespace DSP
} // namespace Krate
