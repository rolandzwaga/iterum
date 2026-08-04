#pragma once

// ==============================================================================
// CloudView - the constellation (Seraphis Phase 11)
// ==============================================================================
// Spec:  specs/seraphis-phase11-ui/spec.md  (FR-006, FR-017 - FR-019, FR-028,
//                                            SC-020, SC-023, SC-024, SC-032)
// Plan:  specs/seraphis-phase11-ui/plan.md  (section 8.1)
// Tasks: specs/seraphis-phase11-ui/tasks.md (T015)
//
// One CView that draws the live harmonic cloud as a point field and, in Edit
// mode, turns pointer gestures into Seraphis::UI::EditMessage sends. It owns NO
// DSP and NO parameter: everything it paints comes from the controller's cached
// CloudFrame, and everything it authors leaves as a message.
//
// THREADING. Constructed, drawn, timed and clicked on the UI thread only. It
// reads Controller::cachedCloudFrame(), which the controller writes on the UI
// thread as well (Controller::queueOpened answers dispatchOnBackgroundThread =
// false, controller.cpp:235-242), so no lock is involved anywhere here. Nothing
// in this file is reachable from process(); SC-011's audio-thread source corpus
// deliberately does not include cloud_view.cpp.
//
// MASK POLARITY. CloudFrame::maskBits uses the PLUGIN sense - bit i set <=>
// partial i is masked (cloud_frame.h:35). That is the opposite of
// HarmonicCloud::setPartialMask(index, active)'s `active` flag; the inversion
// happens once, on the processor side. This view only ever sees maskBits.
// ==============================================================================

#include "processor/cloud_frame.h"
#include "ui/edit_message.h"

#include "vstgui/lib/cbuttonstate.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/vstguifwd.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Seraphis {
class Controller;
}  // namespace Seraphis

namespace Seraphis::UI {

// ==============================================================================
// Geometry / interaction constants (FR-017, FR-028)
// ==============================================================================

/// FIXED frequency span - never an autoscale. With kCloudStereoSpreadId = 0 all
/// 64 points are coincident, and an autoscaled axis would then divide by zero.
inline constexpr float kViewMinHz = 20.0f;
inline constexpr float kViewMaxHz = 20000.0f;

/// A silent UNMASKED partial dissolves to nothing rather than vanishing with a
/// discontinuity, so the floor is literally zero (FR-017). The radius map is
/// PERCEPTUAL (quartic root of amplitude, cloud_view.cpp radiusFromAmplitude) -
/// a linear map drew typical normalized amplitudes (1/8 .. 1/64) as sub-pixel
/// dots (2026-08-04 fix).
inline constexpr VSTGUI::CCoord kMinRadius = 0.0;
inline constexpr VSTGUI::CCoord kMaxRadius = 9.0;

/// The ONE case where radius is not a monotone function of amplitude (Q5): a
/// masked partial's amplitude has smoothed to 0, but it must stay a click
/// target for the un-mask gesture, so it draws as a hollow ring of fixed size.
inline constexpr VSTGUI::CCoord kMaskedRingRadius = 4.0;

inline constexpr VSTGUI::CCoord kHitRadiusPx  = 12.0;
inline constexpr VSTGUI::CCoord kClickSlopPx  = 3.0;
inline constexpr VSTGUI::CCoord kRingLineWidth = 1.5;

/// C4. Used as the ratio<->Hz reference when no voice is sounding, so authoring
/// works identically with and without a held note (Q6, SC-024).
inline constexpr float kFallbackReferenceHz = 261.63f;

/// C-8's 30 Hz redraw budget. The gesture throttle uses the same number, so the
/// message rate can never exceed the rate the view can show.
inline constexpr std::uint32_t kCloudViewTimerMs = 33;

// ==============================================================================
// CloudView
// ==============================================================================

class CloudView : public VSTGUI::CView {
public:
    CloudView(const VSTGUI::CRect& size, Controller* controller);

    /// Copy constructor for CLASS_METHODS' newCopy(). The timer and the
    /// in-flight drag are deliberately NOT copied - a duplicated view starts
    /// with no timer (attached() creates one) and no gesture.
    CloudView(const CloudView& other);

    CloudView& operator=(const CloudView&) = delete;

    ~CloudView() override = default;

    // --- VSTGUI hooks ------------------------------------------------------
    void draw(VSTGUI::CDrawContext* context) override;
    bool attached(VSTGUI::CView* parent) override;
    bool removed(VSTGUI::CView* parent) override;
    void invalid() override;

    VSTGUI::CMouseEventResult onMouseDown(VSTGUI::CPoint& where,
                                          const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseMoved(VSTGUI::CPoint& where,
                                           const VSTGUI::CButtonState& buttons) override;
    VSTGUI::CMouseEventResult onMouseUp(VSTGUI::CPoint& where,
                                        const VSTGUI::CButtonState& buttons) override;

    // --- mode / slot -------------------------------------------------------
    enum class Mode { Observe, Edit };

    void setMode(Mode m) noexcept;
    [[nodiscard]] Mode mode() const noexcept { return mode_; }

    void setSelectedSlot(int slot) noexcept;
    [[nodiscard]] int selectedSlot() const noexcept { return selectedSlot_; }

    // --- the axis map, and its inverse (FR-017, Q6) ------------------------
    // These are the SAME functions draw() and the gesture handlers call; the
    // *ForTest wrappers below exist only so the plan's seam names are present.

    [[nodiscard]] VSTGUI::CCoord yFromHz(float hz) const noexcept;
    [[nodiscard]] float hzFromY(VSTGUI::CCoord y) const noexcept;
    [[nodiscard]] VSTGUI::CCoord xFromPosition(float position) const noexcept;
    [[nodiscard]] float positionFromX(VSTGUI::CCoord x) const noexcept;
    [[nodiscard]] float amplitudeFromY(VSTGUI::CCoord y) const noexcept;

    /// The ratio<->Hz reference for the vertical drag. NEVER CloudFrame::
    /// frequencyHz[i] - that is drift-inclusive by definition (C-2 clause 3)
    /// and using it would bake momentary Brownian detune into a stored ratio.
    [[nodiscard]] float referenceHz() const noexcept;

    // --- test seams --------------------------------------------------------
    [[nodiscard]] std::size_t invalidCountForTest() const noexcept { return invalidCount_; }
    [[nodiscard]] std::size_t drawCountForTest() const noexcept { return drawCount_; }
    [[nodiscard]] std::size_t pointsDrawnForTest() const noexcept { return drawnPoints_.size(); }

    /// Exactly the 33 ms timer body: re-read the cached frame, and invalidate
    /// ONLY when its sequence moved.
    void onTimerForTest() noexcept;

    [[nodiscard]] VSTGUI::CCoord yFromHzForTest(float hz) const noexcept { return yFromHz(hz); }
    [[nodiscard]] VSTGUI::CCoord xFromPositionForTest(float p) const noexcept {
        return xFromPosition(p);
    }
    [[nodiscard]] float hzFromYForTest(VSTGUI::CCoord y) const noexcept { return hzFromY(y); }
    [[nodiscard]] float positionFromXForTest(VSTGUI::CCoord x) const noexcept {
        return positionFromX(x);
    }
    [[nodiscard]] float amplitudeFromYForTest(VSTGUI::CCoord y) const noexcept {
        return amplitudeFromY(y);
    }

    struct DrawnPoint {
        VSTGUI::CCoord x = 0.0;
        VSTGUI::CCoord y = 0.0;
        VSTGUI::CCoord radius = 0.0;
        bool hollow = false;
    };

    [[nodiscard]] const std::vector<DrawnPoint>& drawnPointsForTest() const noexcept {
        return drawnPoints_;
    }

    /// Nearest drawn point within its hit radius; -1 for a miss. A masked point
    /// is hit-tested at kMaskedRingRadius, which is what keeps it clickable
    /// after its amplitude has smoothed to zero (Q5).
    [[nodiscard]] int hitTestForTest(const VSTGUI::CPoint& p) const noexcept;

    /// THE ONLY HEADLESS WAY TO MAKE draw() RUN.
    ///
    /// Nothing in the test harness ever paints: exerciseEditorLifecycle calls
    /// only IPlugView::attached(nullptr, ...) / removed()
    /// (tests/test_helpers/editor_lifecycle_harness.h:98-133) and its own banner
    /// records that the platform attach is a no-op (`CFrame::open(nullptr)`
    /// returns false harmlessly, :12-13). No platform window means no paint
    /// cycle and no CDrawContext.
    ///
    /// So this seam paints into a COffscreenContext when a platform graphics
    /// device can exist (i.e. the view has a live CFrame), and otherwise takes
    /// the plan's sanctioned fallback: the SAME draw() with a null context,
    /// whose emitting half is null-tolerant by construction. Either way
    /// drawCountForTest(), pointsDrawnForTest() and drawnPointsForTest() reflect
    /// the real body rather than a re-implementation of it.
    ///
    /// NOT `noexcept`, and the plan's sketch (plan.md:1616) that wrote it that
    /// way was wrong rather than this being a relaxation. The seam's whole point
    /// is that it runs the REAL `draw()`, `draw()` is a non-`noexcept` virtual,
    /// and its point-building half grows a `std::vector` - so the `noexcept` was
    /// a claim the body could not keep, and `bugprone-exception-escape` says so.
    /// Nothing asserts on the specifier: SC-020(f)/(g) and SC-023 read
    /// `drawCountForTest()`, `pointsDrawnForTest()` and `drawnPointsForTest()`,
    /// none of which change. Making the body swallow with `catch (...)` instead
    /// would have hidden a real failure from exactly the ASan lane SC-023 runs in.
    void renderForTest();

    CLASS_METHODS(CloudView, VSTGUI::CView)

private:
    /// The frame the view paints. Value semantics all the way down: the
    /// controller's cache is a VALUE member, never a pointer, so a draw() with
    /// no frame ever received simply sees partialCount == 0 (FR-019).
    [[nodiscard]] const CloudFrame& currentFrame() const noexcept;

    /// draw()'s point-building half. Pure computation; touches no context.
    void buildPoints();

    /// draw()'s emitting half. A null context is a defined no-op - see
    /// renderForTest().
    void emit(VSTGUI::CDrawContext* context) const;

    /// Emit one EditMessage through the controller, if there is one.
    void send(std::uint8_t kind, std::uint16_t index, float a, float b) const;

    Controller* controller_ = nullptr;

    Mode mode_ = Mode::Observe;   // FR-027's default
    int selectedSlot_ = 0;

    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    std::uint32_t lastSeenSequence_ = 0;
    bool haveSeenAFrame_ = false;

    std::vector<DrawnPoint> drawnPoints_;

    // In-flight gesture state (Edit mode only).
    bool dragging_ = false;
    bool gestureEmitted_ = false;
    int dragIndex_ = -1;
    VSTGUI::CPoint dragStart_{};
    VSTGUI::CPoint dragLast_{};

    std::size_t invalidCount_ = 0;
    std::size_t drawCount_ = 0;
};

}  // namespace Seraphis::UI
