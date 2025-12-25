# Layer 3: System Components - Implementation Tracker

This document tracks the breakdown of Layer 3 roadmap components into focused micro-specs.

**Last Updated**: 2025-12-25

---

## Roadmap Component: 3.1 Delay Engine

The roadmap defines a full-featured Delay Engine. We split this into focused specs:

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **018-delay-engine** | Core wrapper, time modes (ms/sync), smooth changes, dry/wet | ✅ DONE | DelayLine, Smoother, BlockContext |

### 018-delay-engine (Focused Scope)

**Includes:**
- `DelayEngine` class wrapping DelayLine
- Time modes: Free (milliseconds) and Synced (NoteValue via BlockContext)
- Smooth delay time changes via OnePoleSmoother
- Dry/wet mix with kill-dry option
- `prepare()`, `process()`, `reset()` interface

**Explicitly Excludes (for later specs):**
- Tap tempo (separate utility - future)
- Crossfade for instant time changes (only if needed - future)
- Feedback processing (that's FeedbackNetwork - 019)
- Multi-tap (that's TapManager - later)

**Estimated effort**: ~200 LOC, ~15 test cases

---

## Roadmap Component: 3.2 Feedback Network

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **019-feedback-network** | Feedback loop, filter/saturation in path, freeze mode, cross-feedback | ✅ DONE | DelayEngine, MultimodeFilter, SaturationProcessor |

---

## Roadmap Component: 3.3 Modulation Matrix

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **020-modulation-matrix** | Source→Destination routing, depth control | ✅ DONE | LFO, EnvelopeFollower |

---

## Roadmap Component: 3.4 Character Processor

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **021-character-processor** | Tape/BBD/Digital character modes | ✅ DONE | Saturator, NoiseGenerator, Filter, LFO |

---

## Roadmap Component: 3.5 Stereo Field

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **022-stereo-field** | Mono/Stereo/PingPong/DualMono/MidSide modes, width, pan, L/R offset/ratio | ✅ DONE | MidSide, DelayEngine |

---

## Roadmap Component: 3.6 Tap Manager

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **023-tap-manager** | Up to 16 taps, per-tap time/level/pan/filter/feedback, preset patterns, tempo sync | ✅ DONE | DelayLine, Biquad, BlockContext, NoteValue |

---

## Status Legend

- 🔲 TODO - Not started
- 📋 SPECIFIED - Spec written, ready for planning
- 🔄 IN PROGRESS - Currently being implemented
- ✅ DONE - Completed and merged
- 🔲 DEFERRED - May not be needed, will evaluate later

---

## Notes

### Why Split?

Context compaction in long sessions causes focus loss. Smaller specs (~200 LOC, ~15 tests) stay focused and complete cleanly.

### Dependency Order

Recommended implementation order based on dependencies:
1. 018-delay-engine (foundational) ✅ DONE
2. 019-feedback-network (needs DelayEngine) ✅ DONE
3. 020-modulation-matrix (parallel with feedback) ✅ DONE
4. 021-character-processor (needs several L2 processors) ✅ DONE
5. 022-stereo-field (needs DelayEngine, MidSide) ✅ DONE
6. 023-tap-manager (needs DelayLine, Biquad, BlockContext) ✅ DONE

### Future Utilities (implement when needed)

- **tap-tempo**: Tap interval tracking, BPM output - implement when needed for UI
- **delay-crossfade**: Only implement if testing shows OnePoleSmoother isn't sufficient for time changes
