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
// 2026-08-05 re-measurement: Apple Clang (Xcode 26.6, -ffast-math) widened the
// real toolchain spread past the original bounds — the diffusion-network
// zero-mod and spectral-morph render comparisons measured worst metric
// relative error 2.0041e-5 and worst sample error 8.96e-5 on the macOS CI
// leg (previously green on MSVC, GCC and Xcode 26.5). Bounds re-derived from
// those measurements with the same ~2.5-3x headroom. Still 4+ orders below
// any real DSP change (the injected stale-sweep-cache bug moves samples by
// 0.38 against these bounds).
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
inline constexpr float kSampleTolerance = 3.0e-4f;

/// Largest relative aggregate-metric difference treated as toolchain noise.
inline constexpr double kMetricTolerance = 5.0e-5;

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
    std::string detail;

    [[nodiscard]] bool withinTolerance() const {
        return worstMetricRelativeError <= kMetricTolerance && worstSampleError <= kSampleTolerance;
    }
};

/// Compare a freshly rendered fingerprint against a pinned reference.
[[nodiscard]] inline FingerprintComparison compareFingerprints(const RenderFingerprint& actual,
                                                               const RenderFingerprint& reference) {
    FingerprintComparison out;

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
            if (err > kSampleTolerance) {
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
