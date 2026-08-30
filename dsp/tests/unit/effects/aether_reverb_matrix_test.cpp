// ==============================================================================
// Layer 4: Effect Tests - AetherReverb, matrix morph (SC-004)
//                                        (specs/seraphis-phase6-aether-space)
// ==============================================================================
// Constitution Principle XII: Test-First Development.
//
// Reference: specs/seraphis-phase6-aether-space/spec.md
//            specs/seraphis-phase6-aether-space/plan.md   (S1.1, S7.4, S7.5, S8.3)
//            specs/seraphis-phase6-aether-space/tasks.md  (T001 creates, T003 fills)
//
// SCOPE OF THIS TU (plan S1.1's TU-ownership table): SC-004, all six clauses -
//   the three endpoints, orthogonality along the whole morph path, the geodesic
//   Schur reduction and the det = -1 component pinning.
//
// COMPILE FLAGS: none. This TU renders no audio, so it is absent from both the
//   -fno-fast-math list and the -O2 cap list in dsp/tests/CMakeLists.txt.
//
// EVERY linear-algebra reference below is recomputed IN THIS FILE, in double,
// from the floats the engine hands out. Nothing is compared against a stored
// constant derived from the implementation, and no float bit pattern is pinned.
// ==============================================================================

#include <catch2/catch_all.hpp>

#include <krate/dsp/core/random.h>
#include <krate/dsp/effects/aether_reverb.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using Krate::DSP::AetherReverb;
using Krate::DSP::Xorshift32;

namespace {

// -----------------------------------------------------------------------------
// Minimal double-precision matrix toolkit. Row-major, n x n, n <= 16.
// -----------------------------------------------------------------------------

using Mat = std::vector<double>;

[[nodiscard]] Mat makeIdentity(std::size_t n) {
    Mat m(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        m[(i * n) + i] = 1.0;
    }
    return m;
}

[[nodiscard]] Mat matMul(const Mat& a, const Mat& b, std::size_t n) {
    Mat c(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t k = 0; k < n; ++k) {
            const double aik = a[(i * n) + k];
            for (std::size_t j = 0; j < n; ++j) {
                c[(i * n) + j] += aik * b[(k * n) + j];
            }
        }
    }
    return c;
}

[[nodiscard]] Mat matTranspose(const Mat& a, std::size_t n) {
    Mat c(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            c[(i * n) + j] = a[(j * n) + i];
        }
    }
    return c;
}

[[nodiscard]] double frobDiff(const Mat& a, const Mat& b) {
    double acc = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double d = a[i] - b[i];
        acc += d * d;
    }
    return std::sqrt(acc);
}

[[nodiscard]] double maxAbsDiff(const Mat& a, const Mat& b) {
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

/// ||M^T M - I||_F. The SAME accumulation order the engine's cached
/// getMatrixOrthogonalityError() uses, so clause 1's 1e-6 agreement bound is a
/// statement about the engine's bookkeeping, not about summation order.
[[nodiscard]] double orthoError(const Mat& m, std::size_t n) {
    const Mat g = matMul(matTranspose(m, n), m, n);
    return frobDiff(g, makeIdentity(n));
}

/// Determinant by LU with partial pivoting (SC-004 clause 5's stated method).
[[nodiscard]] double determinant(Mat m, std::size_t n) {
    double det = 1.0;
    for (std::size_t c = 0; c < n; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r) {
            if (std::abs(m[(r * n) + c]) > std::abs(m[(pivot * n) + c])) {
                pivot = r;
            }
        }
        if (std::abs(m[(pivot * n) + c]) < 1e-300) {
            return 0.0;
        }
        if (pivot != c) {
            for (std::size_t j = 0; j < n; ++j) {
                std::swap(m[(pivot * n) + j], m[(c * n) + j]);
            }
            det = -det;
        }
        det *= m[(c * n) + c];
        for (std::size_t r = c + 1; r < n; ++r) {
            const double f = m[(r * n) + c] / m[(c * n) + c];
            for (std::size_t j = c; j < n; ++j) {
                m[(r * n) + j] -= f * m[(c * n) + j];
            }
        }
    }
    return det;
}

/// Eigenvalues of a symmetric matrix, ascending. Cyclic Jacobi.
[[nodiscard]] std::vector<double> symmetricEigenvalues(Mat s, std::size_t n) {
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                off += s[(i * n) + j] * s[(i * n) + j];
            }
        }
        if (std::sqrt(2.0 * off) < 1e-14) {
            break;
        }
        for (std::size_t p = 0; p + 1 < n; ++p) {
            for (std::size_t q = p + 1; q < n; ++q) {
                const double apq = s[(p * n) + q];
                if (std::abs(apq) < 1e-300) {
                    continue;
                }
                const double theta = (s[(q * n) + q] - s[(p * n) + p]) / (2.0 * apq);
                const double t = ((theta >= 0.0) ? 1.0 : -1.0) /
                                 (std::abs(theta) + std::sqrt((theta * theta) + 1.0));
                const double c = 1.0 / std::sqrt((t * t) + 1.0);
                const double sn = t * c;
                for (std::size_t k = 0; k < n; ++k) {
                    const double kp = s[(k * n) + p];
                    const double kq = s[(k * n) + q];
                    s[(k * n) + p] = (c * kp) - (sn * kq);
                    s[(k * n) + q] = (sn * kp) + (c * kq);
                }
                for (std::size_t k = 0; k < n; ++k) {
                    const double pk = s[(p * n) + k];
                    const double qk = s[(q * n) + k];
                    s[(p * n) + k] = (c * pk) - (sn * qk);
                    s[(q * n) + k] = (sn * pk) + (c * qk);
                }
            }
        }
    }
    std::vector<double> ev(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        ev[i] = s[(i * n) + i];
    }
    std::sort(ev.begin(), ev.end());
    return ev;
}

/// Smallest singular value: sqrt of the smallest eigenvalue of M^T M.
[[nodiscard]] double smallestSingularValue(const Mat& m, std::size_t n) {
    const std::vector<double> ev = symmetricEigenvalues(matMul(matTranspose(m, n), m, n), n);
    return std::sqrt(std::max(0.0, ev.front()));
}

[[nodiscard]] Mat lerpMatrix(const Mat& a, const Mat& b, double u) {
    Mat out(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
        out[i] = ((1.0 - u) * a[i]) + (u * b[i]);
    }
    return out;
}

[[nodiscard]] Mat toDouble(const std::vector<float>& f) {
    Mat out(f.size(), 0.0);
    for (std::size_t i = 0; i < f.size(); ++i) {
        out[i] = static_cast<double>(f[i]);
    }
    return out;
}

[[nodiscard]] std::vector<float> toFloat(const Mat& d) {
    std::vector<float> out(d.size(), 0.0f);
    for (std::size_t i = 0; i < d.size(); ++i) {
        out[i] = static_cast<float>(d[i]);
    }
    return out;
}

// -----------------------------------------------------------------------------
// Independent constructions of the two analytic endpoints (SC-004 clause 4).
// These are written from the SPEC, not from the header, so they are a real
// cross-check: FR-020 / plan S7.4.
// -----------------------------------------------------------------------------

/// M0 = I - (2/N) * J. Dense: diagonal 1 - 2/N, every off-diagonal -2/N.
[[nodiscard]] Mat householderEndpoint(std::size_t n) {
    Mat m(n * n, 0.0);
    const double c = 2.0 / static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            m[(i * n) + j] = ((i == j) ? 1.0 : 0.0) - c;
        }
    }
    return m;
}

[[nodiscard]] int parityOfPopcount(std::size_t v) {
    int bits = 0;
    while (v != 0u) {
        bits += static_cast<int>(v & 1u);
        v >>= 1u;
    }
    return bits & 1;
}

/// M1 = D * H_N / sqrt(N) with D = diag(-1, 1, ..., 1). Sylvester Hadamard in
/// natural order is H[i][j] = (-1)^popcount(i & j) - exactly what the FWHT
/// butterfly of fdn_reverb.h:696-729 computes. ROW 0 IS NEGATED (C-8).
[[nodiscard]] Mat hadamardEndpoint(std::size_t n) {
    Mat m(n * n, 0.0);
    const double s = 1.0 / std::sqrt(static_cast<double>(n));
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            m[(i * n) + j] = ((parityOfPopcount(i & j) != 0) ? -s : s);
        }
    }
    for (std::size_t j = 0; j < n; ++j) {
        m[j] = -m[j];
    }
    return m;
}

/// B(u * theta): block-diagonal 2x2 rotations [[cos, -sin], [sin, cos]].
[[nodiscard]] Mat blockRotation(const std::vector<float>& thetas, std::size_t n, double u) {
    Mat b(n * n, 0.0);
    for (std::size_t blk = 0; blk < (n / 2u); ++blk) {
        const double angle = u * static_cast<double>(thetas[blk]);
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const std::size_t r0 = 2u * blk;
        const std::size_t r1 = r0 + 1u;
        b[(r0 * n) + r0] = c;
        b[(r0 * n) + r1] = -s;
        b[(r1 * n) + r0] = s;
        b[(r1 * n) + r1] = c;
    }
    return b;
}

// -----------------------------------------------------------------------------
// Engine fixture. P-1 (life modulation off) so the morph position is EXACTLY
// the setDimensionality target, and FR-009's smoother-initialisation rule makes
// it settled on the very first control chunk.
// -----------------------------------------------------------------------------

void prepareEngine(AetherReverb& engine, std::size_t n, std::uint32_t seed) {
    AetherReverb::PrepareConfig cfg;
    cfg.numChannels = n;
    cfg.maxBlockSamples = AetherReverb::kControlChunkSamples;
    cfg.shimmerEnabled = false;           // irrelevant to the matrix; keeps the TU fast (B-1)
    cfg.bloomEnabled = false;
    cfg.spectralDiffusionEnabled = false;
    cfg.seed = seed;
    engine.prepare(48000.0, cfg);
    // P-1
    engine.setSizeBreathDepth(0.0f);
    engine.setDimensionalityTideDepth(0.0f);
    engine.setModDepth(0.0f);
}

/// reset(); setDimensionality(t); one 64-sample silent chunk; copyCurrentMatrix.
[[nodiscard]] Mat materialiseMatrix(AetherReverb& engine, float t, std::size_t n) {
    std::array<float, AetherReverb::kControlChunkSamples> silence{};
    std::array<float, AetherReverb::kControlChunkSamples> outLeft{};
    std::array<float, AetherReverb::kControlChunkSamples> outRight{};
    engine.reset();
    engine.setDimensionality(t);
    engine.processStereoBlock(silence.data(), silence.data(), outLeft.data(), outRight.data(),
                              silence.size());
    std::vector<float> raw(n * n, 0.0f);
    engine.copyCurrentMatrix(raw.data(), n);
    return toDouble(raw);
}

// -----------------------------------------------------------------------------
// Random SO(n) generator for SC-004 clause 6 (ii): Gaussian-ish Xorshift32 draws
// -> modified Gram-Schmidt -> negate a column if det < 0.
// -----------------------------------------------------------------------------

[[nodiscard]] Mat randomSpecialOrthogonal(std::size_t n, std::uint32_t seed) {
    Xorshift32 rng(seed);
    Mat q(n * n, 0.0);
    Mat raw(n * n, 0.0);
    for (std::size_t i = 0; i < (n * n); ++i) {
        // Irwin-Hall(4): a cheap Gaussian-ish draw from the uniform generator.
        double acc = 0.0;
        for (int k = 0; k < 4; ++k) {
            acc += static_cast<double>(rng.nextFloat());
        }
        raw[i] = acc;
    }
    for (std::size_t j = 0; j < n; ++j) {
        std::vector<double> v(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            v[i] = raw[(i * n) + j];
        }
        for (std::size_t k = 0; k < j; ++k) {
            double dot = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                dot += q[(i * n) + k] * v[i];
            }
            for (std::size_t i = 0; i < n; ++i) {
                v[i] -= dot * q[(i * n) + k];
            }
        }
        double norm = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            norm += v[i] * v[i];
        }
        norm = std::sqrt(norm);
        for (std::size_t i = 0; i < n; ++i) {
            q[(i * n) + j] = v[i] / norm;
        }
    }
    if (determinant(q, n) < 0.0) {
        for (std::size_t i = 0; i < n; ++i) {
            q[i * n] = -q[i * n];
        }
    }
    return q;
}

// -----------------------------------------------------------------------------
// SC-004 clause 6 clauses (a), (b), (c) against one R.
// -----------------------------------------------------------------------------

struct Reduction {
    bool ok = false;
    std::vector<float> v;
    std::vector<float> thetas;
    Mat rFloat;  ///< the input EXACTLY as schurReduceSO saw it (float-rounded)
};

[[nodiscard]] Reduction reduce(const Mat& r, std::size_t n) {
    Reduction out;
    const std::vector<float> rf = toFloat(r);
    out.rFloat = toDouble(rf);
    out.v.assign(n * n, 0.0f);
    out.thetas.assign(n / 2u, 0.0f);
    out.ok = AetherReverb::schurReduceSO(rf.data(), n, out.v.data(), out.thetas.data());
    return out;
}

void checkSchurAbc(const Mat& r, std::size_t n, const std::string& label) {
    INFO("schurReduceSO case: " << label << " (n = " << n << ")");
    const Reduction red = reduce(r, n);
    REQUIRE(red.ok);

    const Mat v = toDouble(red.v);

    // (a) V orthogonal.
    const double vOrth = orthoError(v, n);
    INFO("||V^T V - I||_F = " << vOrth);
    REQUIRE(vOrth <= 1e-6);

    // (b) V^T R V is block-diagonal, and each block is the canonical rotation
    //     through the returned theta.
    const Mat c = matMul(matMul(matTranspose(v, n), red.rFloat, n), v, n);
    double worstOffBlock = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            const bool sameBlock = ((i / 2u) == (j / 2u));
            if (!sameBlock) {
                worstOffBlock = std::max(worstOffBlock, std::abs(c[(i * n) + j]));
            }
        }
    }
    INFO("worst off-block |V^T R V| = " << worstOffBlock);
    REQUIRE(worstOffBlock <= 1e-6);

    for (std::size_t blk = 0; blk < (n / 2u); ++blk) {
        const std::size_t r0 = 2u * blk;
        const std::size_t r1 = r0 + 1u;
        const double b00 = c[(r0 * n) + r0];
        const double b01 = c[(r0 * n) + r1];
        const double b10 = c[(r1 * n) + r0];
        const double b11 = c[(r1 * n) + r1];
        INFO("block " << blk << ": [[" << b00 << ", " << b01 << "], [" << b10 << ", " << b11
                      << "]], theta = " << red.thetas[blk]);
        REQUIRE(std::abs(b00 - b11) <= 1e-6);
        REQUIRE(std::abs(b01 + b10) <= 1e-6);
        REQUIRE(std::abs(((b00 * b00) + (b01 * b01)) - 1.0) <= 1e-6);
        // canonical orientation [[cos, -sin], [sin, cos]]
        const double theta = static_cast<double>(red.thetas[blk]);
        REQUIRE(std::abs(b00 - std::cos(theta)) <= 1e-6);
        REQUIRE(std::abs(b10 - std::sin(theta)) <= 1e-6);
    }

    // (c) reconstruction.
    const Mat rebuilt =
        matMul(matMul(v, blockRotation(red.thetas, n, 1.0), n), matTranspose(v, n), n);
    const double recon = frobDiff(rebuilt, red.rFloat);
    INFO("||V B(theta) V^T - R||_F = " << recon);
    REQUIRE(recon <= 1e-6);
}

}  // namespace

// =============================================================================
// SC-004 clauses 1-5
// =============================================================================

namespace {

/// The N-generic body of clauses 1, 2, 4 and 5, plus the parts of clause 3 that
/// are pinned at both orders.
void runOrthogonalityClauses(std::size_t n) {
    AetherReverb engine;
    prepareEngine(engine, n, 1u);

    // ---- clause 1 + clause 5: 101 morph positions -------------------------
    double worstOrtho = 0.0;
    double worstDetError = 0.0;
    double worstCacheDisagreement = 0.0;
    float worstOrthoAt = 0.0f;
    for (int step = 0; step <= 100; ++step) {
        const float t = static_cast<float>(step) / 100.0f;
        const Mat m = materialiseMatrix(engine, t, n);

        const double err = orthoError(m, n);
        if (err > worstOrtho) {
            worstOrtho = err;
            worstOrthoAt = t;
        }
        const double cached = static_cast<double>(engine.getMatrixOrthogonalityError());
        worstCacheDisagreement = std::max(worstCacheDisagreement, std::abs(cached - err));

        // Clause 5: the det = -1 component invariant (C-8).
        worstDetError = std::max(worstDetError, std::abs(determinant(m, n) + 1.0));

        // The engine must also report the position it actually applied.
        REQUIRE(std::abs(engine.getCurrentMorphPosition() - t) <= 1e-6f);
    }
    INFO("worst ||M^T M - I||_F over 101 positions = " << worstOrtho << " at t = " << worstOrthoAt);
    REQUIRE(worstOrtho <= 1e-5);
    INFO("worst |cached - recomputed| = " << worstCacheDisagreement);
    REQUIRE(worstCacheDisagreement <= 1e-6);
    INFO("worst |det(M(t)) + 1| = " << worstDetError);
    REQUIRE(worstDetError <= 1e-5);

    // ---- clause 2: norm preservation, 64 unit vectors at 21 positions ------
    Xorshift32 vecRng(0x5EED1234u);
    double worstNormDrift = 0.0;
    double worstApplyDisagreement = 0.0;
    for (int step = 0; step <= 20; ++step) {
        const float t = static_cast<float>(step) / 20.0f;
        const Mat m = materialiseMatrix(engine, t, n);

        for (int trial = 0; trial < 64; ++trial) {
            std::array<float, 16> x{};
            double norm2 = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                x[i] = vecRng.nextFloat();
                norm2 += static_cast<double>(x[i]) * static_cast<double>(x[i]);
            }
            const double norm = std::sqrt(norm2);
            REQUIRE(norm > 1e-3);
            for (std::size_t i = 0; i < n; ++i) {
                x[i] = static_cast<float>(static_cast<double>(x[i]) / norm);
            }

            std::array<float, 16> y{};
            engine.applyCurrentMatrix(x.data(), y.data());

            double inNorm2 = 0.0;
            double outNorm2 = 0.0;
            double refDiff2 = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                inNorm2 += static_cast<double>(x[i]) * static_cast<double>(x[i]);
                outNorm2 += static_cast<double>(y[i]) * static_cast<double>(y[i]);
                double ref = 0.0;
                for (std::size_t j = 0; j < n; ++j) {
                    ref += m[(i * n) + j] * static_cast<double>(x[j]);
                }
                const double d = ref - static_cast<double>(y[i]);
                refDiff2 += d * d;
            }
            worstNormDrift =
                std::max(worstNormDrift, std::abs(std::sqrt(outNorm2) - std::sqrt(inNorm2)));
            worstApplyDisagreement = std::max(worstApplyDisagreement, std::sqrt(refDiff2));
        }
    }
    INFO("worst | ||Mx|| - ||x|| | = " << worstNormDrift);
    REQUIRE(worstNormDrift <= 1e-4);
    INFO("worst ||copyCurrentMatrix*x - applyCurrentMatrix(x)|| = " << worstApplyDisagreement);
    REQUIRE(worstApplyDisagreement <= 1e-6);

    // ---- clause 4: endpoint identity --------------------------------------
    const Mat atZero = materialiseMatrix(engine, 0.0f, n);
    const Mat atHalf = materialiseMatrix(engine, 0.5f, n);
    const Mat atOne = materialiseMatrix(engine, 1.0f, n);

    const double householderDiff = maxAbsDiff(atZero, householderEndpoint(n));
    INFO("max|M(0) - (I - (2/N)J)| = " << householderDiff);
    REQUIRE(householderDiff <= 1e-6);
    // It is DENSE - the header banner says so, and the reference above encodes it.
    REQUIRE(std::abs(atZero[0] - (1.0 - (2.0 / static_cast<double>(n)))) <= 1e-6);
    REQUIRE(std::abs(atZero[1] + (2.0 / static_cast<double>(n))) <= 1e-6);

    const Mat hadamard = hadamardEndpoint(n);
    const double hadamardDiff = maxAbsDiff(atHalf, hadamard);
    INFO("max|M(0.5) - D*H_N/sqrt(N)| = " << hadamardDiff);
    REQUIRE(hadamardDiff <= 1e-6);
    // Row 0's flipped sign is the whole point of C-8: entry (0,0) is -1/sqrt(N).
    REQUIRE(atHalf[0] < 0.0);
    REQUIRE(std::abs(atHalf[0] + (1.0 / std::sqrt(static_cast<double>(n)))) <= 1e-6);

    INFO("||M(1)^T M(1) - I||_F = " << orthoError(atOne, n));
    REQUIRE(orthoError(atOne, n) <= 1e-5);

    // Reproducible across two prepares at the same seed.
    AetherReverb sameSeed;
    prepareEngine(sameSeed, n, 1u);
    const Mat atOneAgain = materialiseMatrix(sameSeed, 1.0f, n);
    INFO("max|M(1)[seed 1] - M(1)[seed 1, second prepare]| = " << maxAbsDiff(atOne, atOneAgain));
    REQUIRE(maxAbsDiff(atOne, atOneAgain) <= 1e-6);

    // ... and seed-SENSITIVE.
    AetherReverb otherSeed;
    prepareEngine(otherSeed, n, 20260729u);
    const Mat atOneOther = materialiseMatrix(otherSeed, 1.0f, n);
    const double seedSpread = maxAbsDiff(atOne, atOneOther);
    INFO("max|M(1)[seed 1] - M(1)[seed 20260729]| = " << seedSpread);
    REQUIRE(seedSpread >= 0.1);

    // ---- clause 3, the N-generic half: the naive lerp of the SAME shipped
    //      endpoints is singular at t = 0.25 (u = 0.5), and segment 2 at
    //      t = 0.75 misses clause 1's bound by >= 4 orders of magnitude.
    const Mat seg1Mid = lerpMatrix(atZero, atHalf, 0.5);
    const double seg1MidSigma = smallestSingularValue(seg1Mid, n);
    const double seg1MidDet = std::abs(determinant(seg1Mid, n));
    INFO("naive lerp at t=0.25: sigma_min = " << seg1MidSigma << ", |det| = " << seg1MidDet);
    REQUIRE(seg1MidSigma <= 1e-6);
    REQUIRE(seg1MidDet <= 1e-6);

    const Mat seg2Mid = lerpMatrix(atHalf, atOne, 0.5);
    const double seg2MidOrtho = orthoError(seg2Mid, n);
    INFO("naive lerp at t=0.75: ||M^T M - I||_F = " << seg2MidOrtho
                                                    << " (clause 1's bound is 1e-5)");
    REQUIRE(seg2MidOrtho >= 1e-5 * 1e4);

    // ... while the shipped geodesic at the same two positions is orthogonal.
    REQUIRE(orthoError(materialiseMatrix(engine, 0.25f, n), n) <= 1e-5);
    REQUIRE(orthoError(materialiseMatrix(engine, 0.75f, n), n) <= 1e-5);
}

}  // namespace

TEST_CASE("AetherReverb_MatrixOrthogonality", "[effects][aether]") {
    constexpr std::size_t kN = 8;
    runOrthogonalityClauses(kN);

    // ---- clause 3's pinned table, N = 8, in GLOBAL t coordinates ----------
    // The negative control that proves the shipped morph is not a lerp. Built
    // from the SAME endpoints the engine ships and measured with the SAME code.
    //
    // These figures are analytic, not empirical: with A, B orthogonal and
    // L = (1-u)A + uB,
    //     L^T L - I = u(1-u) * (R + R^T - 2I),   R = A^T B,
    // so ||L^T L - I||_F = u(1-u) * ||R + R^T - 2I||_F, and for this endpoint
    // pair ||R + R^T - 2I||_F = 8 exactly at N = 8 (R = A^T B is symmetric with
    // eigenvalues +1 and -1, each of multiplicity 4). The u = 0.5 row is the
    // singular one - C-3's "annihilates an entire subspace of the network
    // state".
    AetherReverb engine;
    prepareEngine(engine, kN, 1u);
    const Mat endpointA = materialiseMatrix(engine, 0.0f, kN);
    const Mat endpointB = materialiseMatrix(engine, 0.5f, kN);

    struct Row {
        double t;
        double u;
        double expected;
    };
    const std::array<Row, 6> table{{{0.0625, 0.125, 0.8750},
                                    {0.1250, 0.250, 1.5000},
                                    {0.1875, 0.375, 1.8750},
                                    {0.2500, 0.500, 2.0000},
                                    {0.3750, 0.750, 1.5000},
                                    {0.5000, 1.000, 0.0000}}};
    for (const Row& row : table) {
        const Mat naive = lerpMatrix(endpointA, endpointB, row.u);
        const double measured = orthoError(naive, kN);
        INFO("naive lerp: t = " << row.t << ", u = " << row.u << ", ||M^T M - I||_F = " << measured
                                << ", expected " << row.expected);
        REQUIRE(std::abs(measured - row.expected) <= 1e-3);
    }
}

TEST_CASE("AetherReverb_MatrixOrthogonality_N16", "[effects][aether][.slow]") {
    constexpr std::size_t kN = 16;
    runOrthogonalityClauses(kN);

    // Clause 3 at N = 16: the pinned figure is the u = 0.5 (t = 0.25) one.
    // ||R + R^T - 2I||_F = 8*sqrt(2) there, so 0.25 * 8*sqrt(2) = 2.8284.
    AetherReverb engine;
    prepareEngine(engine, kN, 1u);
    const Mat naive = lerpMatrix(materialiseMatrix(engine, 0.0f, kN),
                                 materialiseMatrix(engine, 0.5f, kN), 0.5);
    const double measured = orthoError(naive, kN);
    INFO("N = 16 naive lerp at t = 0.25: ||M^T M - I||_F = " << measured << ", expected 2.8284");
    REQUIRE(std::abs(measured - 2.8284) <= 1e-3);
}

// =============================================================================
// SC-004 clause 6 - the prepare-time reduction, tested directly
// =============================================================================

TEST_CASE("AetherReverb_SchurReduction", "[effects][aether]") {
    const std::array<std::size_t, 2> orders{std::size_t{8}, std::size_t{16}};
    for (const std::size_t n : orders) {
        AetherReverb engine;
        prepareEngine(engine, n, 1u);
        const Mat m0 = materialiseMatrix(engine, 0.0f, n);
        const Mat m1 = materialiseMatrix(engine, 0.5f, n);
        const Mat m2 = materialiseMatrix(engine, 1.0f, n);

        // ---- (i) the two shipped endpoint pairs, plus (d) endpoint exactness
        const std::array<std::pair<const Mat*, const Mat*>, 2> segments{
            {{&m0, &m1}, {&m1, &m2}}};
        for (std::size_t seg = 0; seg < segments.size(); ++seg) {
            const Mat& a = *segments[seg].first;
            const Mat& b = *segments[seg].second;
            const Mat r = matMul(matTranspose(a, n), b, n);

            checkSchurAbc(r, n, "shipped segment " + std::to_string(seg + 1u));

            const Reduction red = reduce(r, n);
            REQUIRE(red.ok);
            const Mat v = toDouble(red.v);
            const Mat vT = matTranspose(v, n);

            // (d) B(0) = I  =>  A * V * I * V^T == A
            const Mat atStart =
                matMul(a, matMul(matMul(v, blockRotation(red.thetas, n, 0.0), n), vT, n), n);
            const double startErr = frobDiff(atStart, a);
            INFO("n = " << n << " segment " << (seg + 1u)
                        << ": ||A V B(0) V^T - A||_F = " << startErr);
            REQUIRE(startErr <= 1e-6);

            // (d) B(theta) = R  =>  A * V * B(theta) * V^T == B
            const Mat atEnd =
                matMul(a, matMul(matMul(v, blockRotation(red.thetas, n, 1.0), n), vT, n), n);
            const double endErr = frobDiff(atEnd, b);
            INFO("n = " << n << " segment " << (seg + 1u)
                        << ": ||A V B(theta) V^T - B||_F = " << endErr);
            REQUIRE(endErr <= 1e-6);
        }

        // ---- (ii) 32 seeded random SO(n) inputs ---------------------------
        for (std::uint32_t trial = 0; trial < 32u; ++trial) {
            const Mat r = randomSpecialOrthogonal(n, 0xA37E0000u + trial);
            REQUIRE(orthoError(r, n) <= 1e-9);
            REQUIRE(std::abs(determinant(r, n) - 1.0) <= 1e-9);
            checkSchurAbc(r, n, "random SO(n) trial " + std::to_string(trial));
        }

        // ---- (e) degenerate inputs, constructed directly as V * B * V^T ----
        // A random SO(n) draw effectively never produces repeated eigenvalues,
        // theta = 0 or theta = pi, and those are exactly what a hand-written
        // reduction gets wrong.
        const Mat basis = randomSpecialOrthogonal(n, 0xD36E0001u);
        const Mat basisT = matTranspose(basis, n);
        const std::size_t blocks = n / 2u;

        struct DegenerateCase {
            const char* name;
            std::vector<float> thetas;
        };
        std::vector<DegenerateCase> cases;
        cases.push_back({"all theta = 0 (R = I)", std::vector<float>(blocks, 0.0f)});
        cases.push_back({"all theta = pi",
                         std::vector<float>(blocks, static_cast<float>(std::acos(-1.0)))});
        cases.push_back({"repeated eigenvalue (all theta = 0.7)",
                         std::vector<float>(blocks, 0.7f)});
        {
            // Mixed: half at theta = 0, a repeated generic pair, and theta = pi.
            std::vector<float> mixed(blocks, 0.7f);
            mixed[0] = 0.0f;
            mixed[1] = 0.0f;
            mixed[blocks - 1u] = static_cast<float>(std::acos(-1.0));
            cases.push_back({"mixed 0 / repeated / pi", mixed});
        }
        {
            // Two distinct repeated clusters.
            std::vector<float> pairs(blocks, 0.4f);
            for (std::size_t b = blocks / 2u; b < blocks; ++b) {
                pairs[b] = 2.3f;
            }
            cases.push_back({"two distinct repeated clusters", pairs});
        }

        for (const DegenerateCase& degenerate : cases) {
            const Mat r =
                matMul(matMul(basis, blockRotation(degenerate.thetas, n, 1.0), n), basisT, n);
            checkSchurAbc(r, n, std::string("degenerate: ") + degenerate.name);
        }

        // ---- rejection: not numerically in SO(n) --------------------------
        {
            // M0 is orthogonal but det = -1 (it is a single reflection, C-8).
            REQUIRE(std::abs(determinant(m0, n) + 1.0) <= 1e-5);
            const Reduction rejected = reduce(m0, n);
            REQUIRE_FALSE(rejected.ok);
        }
        {
            // Plainly non-orthogonal.
            Mat scaled = m1;
            for (double& value : scaled) {
                value *= 1.5;
            }
            const Reduction rejected = reduce(scaled, n);
            REQUIRE_FALSE(rejected.ok);
        }
    }
}
