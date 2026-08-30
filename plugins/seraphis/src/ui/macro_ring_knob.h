#pragma once

// ==============================================================================
// MacroRingKnob - the five large macro rings (Seraphis Phase 11)
// ==============================================================================
// Spec:  specs/seraphis-phase11-ui/spec.md  (FR-020, FR-021, SC-004)
// Plan:  specs/seraphis-phase11-ui/plan.md  (section 8.2)
// Tasks: specs/seraphis-phase11-ui/tasks.md (T014)
//
// One class, instantiated FIVE times from the uidesc - one per macro parameter
// ID 100..104 (Dream, Bloom, Gravity, Dissolve, Entropy). It is a
// Krate::Plugins::ArcKnob (plugins/shared/src/ui/arc_knob.h:49) with an extra
// outer ring drawn behind the inherited arc; every mouse/wheel/keyboard
// interaction, the value popup and the beginEdit/valueChanged/endEdit gesture
// come from the base class unchanged.
//
// FR-021 - THE PERTURBATION IS THE REAL DSP. This knob does the standard
// beginEdit / performEdit / endEdit on its ParamID (inherited, arc_knob.h:136,
// :174, :190) and does NOTHING to the cloud view. The motion the user sees in
// the constellation is whatever the next CloudFrame reports, i.e. the real
// SeraphisMacroMatrix response read back out of the engine. There is
// deliberately NO view-local animation, NO synthetic displacement and NO
// interpolation toward a target the DSP is not producing - the class holds no
// mutable state at all, which is what makes SC-022(c) (driving the knob leaves
// CloudView::invalidCountForTest() / drawnPointsForTest() unchanged) provable
// rather than merely intended.
//
// Registration is a ViewCreatorAdapter, NOT createCustomView (C-7a): the knob
// must accept control-tag and every other CControl attribute from the uidesc,
// which is exactly what getBaseViewName() -> UIViewCreator::kCControl buys
// (arc_knob.h:562-564). createCustomView views are CViews the factory does not
// decorate with CControl attributes.
// ==============================================================================

#include "ui/arc_knob.h"    // Krate::Plugins::ArcKnob, darkenColor via color_utils.h

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/crect.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"

#include <algorithm>
#include <cstdint>

namespace Seraphis::UI {

// ==============================================================================
// MacroRingKnob
// ==============================================================================

class MacroRingKnob : public Krate::Plugins::ArcKnob {
public:
    MacroRingKnob(const VSTGUI::CRect& size, VSTGUI::IControlListener* l,
                  int32_t tag)
        : ArcKnob(size, l, tag) {}

    MacroRingKnob(const MacroRingKnob& other) : ArcKnob(other) {}

    /// Ring styling BEHIND the inherited arc. ArcKnob::draw() is called last
    /// because it owns the guide ring, value arc, modulation arc, indicator and
    /// the closing setDirty(false) (arc_knob.h:116-125).
    void draw(VSTGUI::CDrawContext* context) override {
        drawOuterRing(context);
        ArcKnob::draw(context);
    }

    CLASS_METHODS(MacroRingKnob, Krate::Plugins::ArcKnob)

private:
    static constexpr VSTGUI::CCoord kRingLineWidth = 2.0;
    static constexpr float kRingDarkenFactor = 0.55f;
    static constexpr std::uint8_t kRingAlpha = 110;

    /// A single full circle just outside ArcKnob's arc radius (which is
    /// dim / 2 - indicatorLength / 2, arc_knob.h:267-271). Stateless: the
    /// geometry comes from getViewSize() and the colour from the inherited
    /// arc colour, so nothing here can drift out of sync with the base class.
    void drawOuterRing(VSTGUI::CDrawContext* context) const {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path)
            return;

        const VSTGUI::CRect vs = getViewSize();
        const double dim = std::min(vs.getWidth(), vs.getHeight());
        const double radius = dim / 2.0 - kRingLineWidth / 2.0;
        if (radius < 1.0)
            return;

        const double cx = vs.left + vs.getWidth() / 2.0;
        const double cy = vs.top + vs.getHeight() / 2.0;
        path->addEllipse(VSTGUI::CRect(cx - radius, cy - radius,
                                       cx + radius, cy + radius));

        VSTGUI::CColor ringColor =
            Krate::Plugins::darkenColor(getArcColor(), kRingDarkenFactor);
        ringColor.alpha = kRingAlpha;

        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);
        context->setFrameColor(ringColor);
        context->setLineWidth(kRingLineWidth);
        context->setLineStyle(VSTGUI::kLineSolid);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }
};

// ==============================================================================
// ViewCreator Registration
// ==============================================================================
// Same shape as ArcKnobCreator (arc_knob.h:555-564). No apply() override: the
// five rings carry only stock CControl attributes in the uidesc, and the
// CControl base creator applies those.

struct MacroRingKnobCreator : VSTGUI::ViewCreatorAdapter {
    MacroRingKnobCreator() { VSTGUI::UIViewFactory::registerViewCreator(*this); }

    VSTGUI::IdStringPtr getViewName() const override { return "MacroRingKnob"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCControl;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Macro Ring Knob";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new MacroRingKnob(VSTGUI::CRect(0, 0, 96, 96), nullptr, -1);
    }
};

// Inline variable (C++17) - safe for inclusion from multiple translation units.
// It only registers in a TU that is actually LINKED, which is why entry.cpp
// includes this header (arc_knob.h:714-716's rule).
inline MacroRingKnobCreator gMacroRingKnobCreator;

}  // namespace Seraphis::UI
