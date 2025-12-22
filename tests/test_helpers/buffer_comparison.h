#pragma once
// ==============================================================================
// Buffer Comparison Utilities
// ==============================================================================
// Tools for comparing audio buffers in tests.
// See specs/TESTING-GUIDE.md for usage guidance.
// ==============================================================================

#include <array>
#include <cmath>
#include <algorithm>
#include <limits>
#include <string>
#include <sstream>
#include <iomanip>

namespace TestHelpers {

// ==============================================================================
// Comparison Results
// ==============================================================================

struct ComparisonResult {
    bool passed = true;
    size_t firstDifferenceIndex = 0;
    float maxDifference = 0.0f;
    float expectedValue = 0.0f;
    float actualValue = 0.0f;

    std::string message() const {
        if (passed) return "Buffers match";

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(8);
        oss << "Buffers differ at index " << firstDifferenceIndex
            << ": expected " << expectedValue
            << ", got " << actualValue
            << " (diff: " << maxDifference << ")";
        return oss.str();
    }

    explicit operator bool() const { return passed; }
};

// ==============================================================================
// Buffer Comparison Functions
// ==============================================================================

// Compare two buffers with absolute tolerance
template <size_t N>
inline ComparisonResult compareBuffers(const std::array<float, N>& expected,
                                       const std::array<float, N>& actual,
                                       float tolerance = 1e-6f) {
    ComparisonResult result;

    for (size_t i = 0; i < N; ++i) {
        float diff = std::abs(expected[i] - actual[i]);
        if (diff > result.maxDifference) {
            result.maxDifference = diff;
        }
        if (diff > tolerance && result.passed) {
            result.passed = false;
            result.firstDifferenceIndex = i;
            result.expectedValue = expected[i];
            result.actualValue = actual[i];
        }
    }

    return result;
}

inline ComparisonResult compareBuffers(const float* expected,
                                       const float* actual,
                                       size_t size,
                                       float tolerance = 1e-6f) {
    ComparisonResult result;

    for (size_t i = 0; i < size; ++i) {
        float diff = std::abs(expected[i] - actual[i]);
        if (diff > result.maxDifference) {
            result.maxDifference = diff;
        }
        if (diff > tolerance && result.passed) {
            result.passed = false;
            result.firstDifferenceIndex = i;
            result.expectedValue = expected[i];
            result.actualValue = actual[i];
        }
    }

    return result;
}

// ==============================================================================
// Buffer Validation Functions
// ==============================================================================

// Check that all samples are finite (no NaN or Inf)
template <size_t N>
inline bool allFinite(const std::array<float, N>& buffer) {
    return std::all_of(buffer.begin(), buffer.end(),
                       [](float s) { return std::isfinite(s); });
}

inline bool allFinite(const float* buffer, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (!std::isfinite(buffer[i])) return false;
    }
    return true;
}

// Check that all samples are within bounds
template <size_t N>
inline bool allWithinBounds(const std::array<float, N>& buffer,
                            float minVal = -1.0f,
                            float maxVal = 1.0f) {
    return std::all_of(buffer.begin(), buffer.end(),
                       [minVal, maxVal](float s) {
                           return s >= minVal && s <= maxVal;
                       });
}

inline bool allWithinBounds(const float* buffer,
                            size_t size,
                            float minVal = -1.0f,
                            float maxVal = 1.0f) {
    for (size_t i = 0; i < size; ++i) {
        if (buffer[i] < minVal || buffer[i] > maxVal) return false;
    }
    return true;
}

// Check for DC offset
template <size_t N>
inline float calculateDCOffset(const std::array<float, N>& buffer) {
    float sum = 0.0f;
    for (float s : buffer) sum += s;
    return sum / static_cast<float>(N);
}

inline float calculateDCOffset(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) sum += buffer[i];
    return sum / static_cast<float>(size);
}

// ==============================================================================
// Analysis Functions
// ==============================================================================

// Find peak absolute value
template <size_t N>
inline float findPeak(const std::array<float, N>& buffer) {
    float peak = 0.0f;
    for (float s : buffer) {
        float absVal = std::abs(s);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

inline float findPeak(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

// Calculate RMS level
template <size_t N>
inline float calculateRMS(const std::array<float, N>& buffer) {
    float sum = 0.0f;
    for (float s : buffer) sum += s * s;
    return std::sqrt(sum / static_cast<float>(N));
}

inline float calculateRMS(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) sum += buffer[i] * buffer[i];
    return std::sqrt(sum / static_cast<float>(size));
}

// Calculate energy (sum of squares)
template <size_t N>
inline float calculateEnergy(const std::array<float, N>& buffer) {
    float sum = 0.0f;
    for (float s : buffer) sum += s * s;
    return sum;
}

// ==============================================================================
// Correlation Functions
// ==============================================================================

// Calculate normalized cross-correlation at zero lag
template <size_t N>
inline float calculateCorrelation(const std::array<float, N>& a,
                                  const std::array<float, N>& b) {
    float sumAB = 0.0f, sumA2 = 0.0f, sumB2 = 0.0f;

    for (size_t i = 0; i < N; ++i) {
        sumAB += a[i] * b[i];
        sumA2 += a[i] * a[i];
        sumB2 += b[i] * b[i];
    }

    float denom = std::sqrt(sumA2 * sumB2);
    if (denom < 1e-10f) return 0.0f;

    return sumAB / denom;
}

// ==============================================================================
// String Conversion (for ApprovalTests)
// ==============================================================================

// Convert buffer to string for approval testing
template <size_t N>
inline std::string bufferToString(const std::array<float, N>& buffer,
                                  size_t stride = 1,
                                  int precision = 6) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision);

    for (size_t i = 0; i < N; i += stride) {
        oss << buffer[i] << "\n";
    }

    return oss.str();
}

inline std::string bufferToString(const float* buffer,
                                  size_t size,
                                  size_t stride = 1,
                                  int precision = 6) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision);

    for (size_t i = 0; i < size; i += stride) {
        oss << buffer[i] << "\n";
    }

    return oss.str();
}

} // namespace TestHelpers
