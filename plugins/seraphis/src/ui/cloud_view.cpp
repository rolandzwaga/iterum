// ==============================================================================
// CloudView - implementation (Seraphis Phase 11, T015)
// ==============================================================================
// Spec:  specs/seraphis-phase11-ui/spec.md  (FR-017 - FR-019, FR-028)
// Plan:  specs/seraphis-phase11-ui/plan.md  (section 8.1)
// ==============================================================================

#include "ui/cloud_view.h"

#include "controller/controller.h"

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/coffscreencontext.h"

#include <algorithm>
#include <cmath>

namespace Seraphis::UI {

namespace {

// --- palette -----------------------------------------------------------------
// Display only. `overriddenBits` tints a point so a user-authored partial reads
// differently from an engine-driven one (FR-017); nothing here feeds a gesture.
const VSTGUI::CColor kBackgroundColor{14, 16, 22, 255};
const VSTGUI::CColor kPartialColor{150, 200, 255, 220};
const VSTGUI::CColor kOverriddenColor{255, 190, 110, 235};
const VSTGUI::CColor kMaskedColor{120, 130, 150, 200};

/// NaN-tolerant clamp. std::clamp propagates a NaN input, and std::isnan is not
/// usable under -ffast-math (which the macOS leg builds with); writing the
/// comparison as `!(v > lo)` sends a NaN to `lo` without naming a predicate the
/// optimiser is allowed to fold away.
[[nodiscard]] float clampToRange(float v, float lo, float hi) noexcept {
    if (!(v > lo)) {
        return lo;
    }
    return (v > hi) ? hi : v;
}

[[nodiscard]] double clampToRange(double v, double lo, double hi) noexcept {
    if (!(v > lo)) {
        return lo;
    }
    return (v > hi) ? hi : v;
}

/// log2 of the fixed span's edges. Computed per call rather than as constexpr
/// because std::log2 is not constexpr; both calls are two instructions and the
/// whole map runs at most 64 times per redraw.
[[nodiscard]] double log2MinHz() noexcept { return std::log2(static_cast<double>(kViewMinHz)); }
[[nodiscard]] double log2MaxHz() noexcept { return std::log2(static_cast<double>(kViewMaxHz)); }

/// The empty frame a view with no controller paints (FR-019). Static so the
/// view itself does not carry a second 808-byte CloudFrame.
[[nodiscard]] const CloudFrame& emptyFrame() noexcept {
    static const CloudFrame kEmpty{};
    return kEmpty;
}

/// The mirror a view with no controller authors against. Value-initialised
/// SpectralState is documented valid (spectral_state.h:42-43).
[[nodiscard]] const Krate::DSP::SpectralState& emptyState() noexcept {
    static const Krate::DSP::SpectralState kEmpty{};
    return kEmpty;
}

}  // namespace

// ==============================================================================
// Construction
// ==============================================================================

CloudView::CloudView(const VSTGUI::CRect& size, Controller* controller)
    : VSTGUI::CView(size), controller_(controller) {
    // Reserved ONCE, here. draw() only ever clear()s and push_back()s into it,
    // and draw() is UI thread, so no allocation is implied anywhere near audio.
    drawnPoints_.reserve(64u);
}

CloudView::CloudView(const CloudView& other)
    : VSTGUI::CView(other),
      controller_(other.controller_),
      mode_(other.mode_),
      selectedSlot_(other.selectedSlot_) {
    drawnPoints_.reserve(64u);
}

// ==============================================================================
// Mode / slot
// ==============================================================================

void CloudView::setMode(Mode m) noexcept {
    if (mode_ == m) {
        return;
    }
    mode_ = m;
    // Leaving Edit mode abandons any in-flight gesture rather than letting the
    // next mouse-up emit against a mode the user has already left.
    dragging_ = false;
    gestureEmitted_ = false;
    dragIndex_ = -1;
    invalid();
}

void CloudView::setSelectedSlot(int slot) noexcept {
    if (slot < 0 || slot > 3 || slot == selectedSlot_) {
        return;
    }
    selectedSlot_ = slot;
}

// ==============================================================================
// The frame
// ==============================================================================

const CloudFrame& CloudView::currentFrame() const noexcept {
    return (controller_ != nullptr) ? controller_->cachedCloudFrame() : emptyFrame();
}

float CloudView::referenceHz() const noexcept {
    const CloudFrame& f = currentFrame();
    if (f.activeVoices > 0 && f.fundamentalHz > 0.0f) {
        return f.fundamentalHz;
    }
    return kFallbackReferenceHz;
}

// ==============================================================================
// The axis map (FR-017) and its inverse (Q6)
// ==============================================================================

VSTGUI::CCoord CloudView::yFromHz(float hz) const noexcept {
    const VSTGUI::CRect vs = getViewSize();
    const double height = vs.getHeight();
    const float clamped = clampToRange(hz, kViewMinHz, kViewMaxHz);
    const double lo = log2MinHz();
    const double hi = log2MaxHz();
    const double u = (std::log2(static_cast<double>(clamped)) - lo) / (hi - lo);
    // INVERTED: a higher frequency draws nearer the top, i.e. at a SMALLER y.
    return vs.bottom - u * height;
}

float CloudView::hzFromY(VSTGUI::CCoord y) const noexcept {
    const VSTGUI::CRect vs = getViewSize();
    const double height = vs.getHeight();
    if (!(height > 0.0)) {
        return kViewMinHz;
    }
    const double u = clampToRange((vs.bottom - y) / height, 0.0, 1.0);
    const double lo = log2MinHz();
    const double hi = log2MaxHz();
    return static_cast<float>(std::exp2(lo + u * (hi - lo)));
}

VSTGUI::CCoord CloudView::xFromPosition(float position) const noexcept {
    const VSTGUI::CRect vs = getViewSize();
    // CLAMPED, never wrapped: position is already [-1, +1] (harmonic_cloud.h:986).
    const double p = static_cast<double>(clampToRange(position, -1.0f, 1.0f));
    return vs.left + (p + 1.0) * 0.5 * vs.getWidth();
}

float CloudView::positionFromX(VSTGUI::CCoord x) const noexcept {
    const VSTGUI::CRect vs = getViewSize();
    const double width = vs.getWidth();
    if (!(width > 0.0)) {
        return 0.0f;
    }
    const double p = ((x - vs.left) / width) * 2.0 - 1.0;
    return static_cast<float>(clampToRange(p, -1.0, 1.0));
}

float CloudView::amplitudeFromY(VSTGUI::CCoord y) const noexcept {
    const VSTGUI::CRect vs = getViewSize();
    const double height = vs.getHeight();
    if (!(height > 0.0)) {
        return 0.0f;
    }
    return static_cast<float>(clampToRange((vs.bottom - y) / height, 0.0, 1.0));
}

// ==============================================================================
// Drawing
// ==============================================================================

void CloudView::buildPoints() {
    drawnPoints_.clear();

    const CloudFrame& f = currentFrame();
    const std::size_t count =
        std::min<std::size_t>(static_cast<std::size_t>(f.partialCount), 64u);

    for (std::size_t i = 0; i < count; ++i) {
        const bool masked = ((f.maskBits >> i) & 1ull) != 0ull;

        DrawnPoint pt{};
        pt.x = xFromPosition(f.position[i]);
        pt.y = yFromHz(f.frequencyHz[i]);

        if (masked) {
            // Q5: fixed ring, REGARDLESS of amplitude. Not culled, not shrunk to
            // kMinRadius - it has to stay a click target for the un-mask gesture.
            pt.radius = kMaskedRingRadius;
            pt.hollow = true;
        } else {
            const double amp = static_cast<double>(clampToRange(f.amplitude[i], 0.0f, 1.0f));
            pt.radius = kMinRadius + amp * (kMaxRadius - kMinRadius);
            pt.hollow = false;
        }

        drawnPoints_.push_back(pt);
    }

    // C-8: in Edit mode the constellation still animates. Only the DRAGGED
    // partial is pinned under the pointer; every other point keeps following
    // the frames.
    if (mode_ == Mode::Edit && dragging_ && dragIndex_ >= 0 &&
        static_cast<std::size_t>(dragIndex_) < drawnPoints_.size()) {
        DrawnPoint& dragged = drawnPoints_[static_cast<std::size_t>(dragIndex_)];
        dragged.x = dragLast_.x;
        dragged.y = dragLast_.y;
    }
}

void CloudView::emit(VSTGUI::CDrawContext* context) const {
    if (context == nullptr) {
        return;  // renderForTest()'s fallback leg - a DEFINED no-op, see the header.
    }

    const VSTGUI::CRect vs = getViewSize();
    context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
    context->setFillColor(kBackgroundColor);
    context->drawRect(vs, VSTGUI::kDrawFilled);

    const CloudFrame& f = currentFrame();

    for (std::size_t i = 0; i < drawnPoints_.size(); ++i) {
        const DrawnPoint& pt = drawnPoints_[i];
        if (!(pt.radius > 0.0)) {
            continue;  // a dissolved partial: still COUNTED, just nothing to paint
        }

        const VSTGUI::CRect r(pt.x - pt.radius, pt.y - pt.radius, pt.x + pt.radius,
                              pt.y + pt.radius);
        const bool overridden = ((f.overriddenBits >> i) & 1ull) != 0ull;

        if (pt.hollow) {
            context->setFrameColor(overridden ? kOverriddenColor : kMaskedColor);
            context->setLineWidth(kRingLineWidth);
            context->drawEllipse(r, VSTGUI::kDrawStroked);
        } else {
            context->setFillColor(overridden ? kOverriddenColor : kPartialColor);
            context->drawEllipse(r, VSTGUI::kDrawFilled);
        }
    }
}

void CloudView::draw(VSTGUI::CDrawContext* context) {
    ++drawCount_;
    buildPoints();
    emit(context);
    setDirty(false);
}

void CloudView::renderForTest() {
    // A live CFrame is the only evidence available here that a platform graphics
    // device exists: VSTGUI::getPlatformFactory() dereferences a global that
    // VSTGUI::init() populates (platformfactory.cpp:21, :45-48), and a test
    // executable never calls init(). Probing it directly would crash rather
    // than fail.
    if (getFrame() != nullptr) {
        const VSTGUI::CPoint size = getViewSize().getSize();
        if (auto offscreen = VSTGUI::COffscreenContext::create(size)) {
            offscreen->beginDraw();
            draw(offscreen.get());
            offscreen->endDraw();
            return;
        }
    }

    // Plan section 8.1's sanctioned fallback: the SAME draw(), whose emitting
    // half tolerates a null context by construction. buildPoints() - the half
    // every criterion in T015 reads - runs identically on both legs.
    draw(nullptr);
}

void CloudView::invalid() {
    ++invalidCount_;
    VSTGUI::CView::invalid();
}

// ==============================================================================
// Timer (C-8, FR-018)
// ==============================================================================

bool CloudView::attached(VSTGUI::CView* parent) {
    const bool ok = VSTGUI::CView::attached(parent);
    if (ok && !timer_) {
        timer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
            [this](VSTGUI::CVSTGUITimer* /*t*/) { onTimerForTest(); }, kCloudViewTimerMs,
            true /* start immediately */));
    }
    return ok;
}

bool CloudView::removed(VSTGUI::CView* parent) {
    // C-7c: cancel BEFORE teardown so no tick dereferences a dying view.
    if (timer_) {
        timer_->stop();
        timer_ = nullptr;
    }
    return VSTGUI::CView::removed(parent);
}

void CloudView::onTimerForTest() noexcept {
    const std::uint32_t seq = currentFrame().sequence;
    if (haveSeenAFrame_ && seq == lastSeenSequence_) {
        return;  // idle transport: one timer callback, NO redraw
    }
    haveSeenAFrame_ = true;
    lastSeenSequence_ = seq;
    invalid();
}

// ==============================================================================
// Hit testing
// ==============================================================================

int CloudView::hitTestForTest(const VSTGUI::CPoint& p) const noexcept {
    int best = -1;
    double bestDistanceSq = 0.0;

    for (std::size_t i = 0; i < drawnPoints_.size(); ++i) {
        const DrawnPoint& pt = drawnPoints_[i];
        const double dx = p.x - pt.x;
        const double dy = p.y - pt.y;
        const double distanceSq = dx * dx + dy * dy;
        // A masked point's own ring radius may exceed the default slop; take the
        // larger so it never becomes harder to click than an audible one.
        const double reach = std::max(kHitRadiusPx, pt.radius);

        if (distanceSq <= reach * reach && (best < 0 || distanceSq < bestDistanceSq)) {
            best = static_cast<int>(i);
            bestDistanceSq = distanceSq;
        }
    }

    return best;
}

// ==============================================================================
// Gestures (FR-028)
// ==============================================================================

void CloudView::send(std::uint8_t kind, std::uint16_t index, float a, float b) const {
    if (controller_ == nullptr) {
        return;
    }
    EditMessage m{};
    m.kind = kind;
    m.slot = static_cast<std::uint8_t>(selectedSlot_);
    m.index = index;
    m.a = a;
    m.b = b;
    controller_->sendEditMessage(m);
}

VSTGUI::CMouseEventResult CloudView::onMouseDown(VSTGUI::CPoint& where,
                                                 const VSTGUI::CButtonState& buttons) {
    if (mode_ != Mode::Edit || !buttons.isLeftButton()) {
        return VSTGUI::kMouseEventNotHandled;
    }

    const int hit = hitTestForTest(where);
    if (hit < 0) {
        return VSTGUI::kMouseEventNotHandled;
    }

    dragging_ = true;
    gestureEmitted_ = false;
    dragIndex_ = hit;
    dragStart_ = where;
    dragLast_ = where;
    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult CloudView::onMouseMoved(VSTGUI::CPoint& where,
                                                  const VSTGUI::CButtonState& buttons) {
    if (!dragging_ || dragIndex_ < 0) {
        return VSTGUI::kMouseEventNotHandled;
    }

    dragLast_ = where;

    const double dx = where.x - dragStart_.x;
    const double dy = where.y - dragStart_.y;
    if (std::abs(dx) <= kClickSlopPx && std::abs(dy) <= kClickSlopPx) {
        // Still inside the slop: this may yet turn out to be a click, so emit
        // nothing. onMouseUp decides.
        return VSTGUI::kMouseEventHandled;
    }

    const auto index = static_cast<std::uint16_t>(dragIndex_);
    const auto mirrorIndex = static_cast<std::size_t>(dragIndex_);

    if (std::abs(dy) > std::abs(dx)) {
        // The mirror is the authoring truth for the field that is NOT moving.
        const Krate::DSP::SpectralState& mirror =
            (controller_ != nullptr) ? controller_->slotMirror(selectedSlot_) : emptyState();
        if ((buttons.getModifierState() & VSTGUI::kAlt) != 0) {
            // Alt + vertical: amplitude moves, ratio is carried through unchanged.
            send(1u, index, mirror.ratios[mirrorIndex], amplitudeFromY(where.y));
        } else {
            // Plain vertical: ratio moves, amplitude is carried through unchanged.
            // referenceHz() is the UNDETUNED f0, never frequencyHz[i].
            const float newRatio = hzFromY(where.y) / referenceHz();
            send(1u, index, newRatio, mirror.amplitudes[mirrorIndex]);
        }
    } else {
        send(2u, index, positionFromX(where.x), 0.0f);
    }

    gestureEmitted_ = true;
    invalid();
    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult CloudView::onMouseUp(VSTGUI::CPoint& where,
                                               const VSTGUI::CButtonState& /*buttons*/) {
    if (!dragging_ || dragIndex_ < 0) {
        return VSTGUI::kMouseEventNotHandled;
    }

    const double dx = where.x - dragStart_.x;
    const double dy = where.y - dragStart_.y;

    if (!gestureEmitted_ && std::abs(dx) <= kClickSlopPx && std::abs(dy) <= kClickSlopPx) {
        // A click, not a drag: TOGGLE the mask from what the frame currently
        // reports (Q5). An unconditional mask would make un-masking unreachable
        // from the UI.
        const std::uint64_t bit = 1ull << static_cast<unsigned>(dragIndex_);
        const bool masked = (currentFrame().maskBits & bit) != 0ull;
        send(3u, static_cast<std::uint16_t>(dragIndex_), masked ? 0.0f : 1.0f, 0.0f);
    }

    dragging_ = false;
    gestureEmitted_ = false;
    dragIndex_ = -1;
    invalid();
    return VSTGUI::kMouseEventHandled;
}

}  // namespace Seraphis::UI
