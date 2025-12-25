# Layer 3: System Components - Implementation Tracker

This document tracks the breakdown of Layer 3 roadmap components into focused micro-specs.

**Last Updated**: 2025-12-25

---

## Roadmap Component: 3.1 Delay Engine

The roadmap defines a full-featured Delay Engine. We split this into focused specs:

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **018-delay-engine** | Core wrapper, time modes (ms/sync), smooth changes, dry/wet | 🔄 IN PROGRESS | DelayLine, Smoother, BlockContext |
| **019-tap-tempo** | Tap interval tracking, BPM output, averaging | 🔲 TODO | None |
| **020-delay-crossfade** | Dual delay lines, crossfade for instant time jumps | 🔲 DEFERRED | DelayEngine |

### 018-delay-engine (Focused Scope)

**Includes:**
- `DelayEngine` class wrapping DelayLine
- Time modes: Free (milliseconds) and Synced (NoteValue via BlockContext)
- Smooth delay time changes via OnePoleSmoother
- Dry/wet mix with kill-dry option
- `prepare()`, `process()`, `reset()` interface

**Explicitly Excludes (for later specs):**
- Tap tempo (separate utility - 019)
- Crossfade for instant time changes (only if testing shows it's needed - 020)
- Feedback processing (that's FeedbackNetwork - 021)
- Multi-tap (that's TapManager - later)

**Estimated effort**: ~200 LOC, ~15 test cases

---

## Roadmap Component: 3.2 Feedback Network

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **021-feedback-network** | Feedback loop, filter/saturation in path, freeze mode | 🔲 TODO | DelayEngine, Filter, Saturator |

---

## Roadmap Component: 3.3 Modulation Matrix

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **022-modulation-matrix** | Source→Destination routing, depth control | 🔲 TODO | LFO, EnvelopeFollower |

---

## Roadmap Component: 3.4 Character Processor

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **023-character-processor** | Tape/BBD/Digital character modes | 🔲 TODO | Saturator, NoiseGenerator, Filter, LFO |

---

## Roadmap Component: 3.5 Stereo Field

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **024-stereo-field** | Mono/Stereo/PingPong/MidSide modes, width | 🔲 TODO | MidSide, DelayEngine |

---

## Roadmap Component: 3.6 Tap Manager

| Spec | Scope | Status | Dependencies |
|------|-------|--------|--------------|
| **025-tap-manager** | Multiple delay engines, per-tap controls | 🔲 TODO | DelayEngine (multiple) |

---

## Status Legend

- 🔲 TODO - Not started
- 🔄 IN PROGRESS - Currently being implemented
- ✅ DONE - Completed and merged
- 🔲 DEFERRED - May not be needed, will evaluate later

---

## Notes

### Why Split?

Context compaction in long sessions causes focus loss. Smaller specs (~200 LOC, ~15 tests) stay focused and complete cleanly.

### Dependency Order

Recommended implementation order based on dependencies:
1. 018-delay-engine (foundational)
2. 019-tap-tempo (optional, small utility)
3. 021-feedback-network (needs DelayEngine)
4. 022-modulation-matrix (parallel with 021)
5. 023-character-processor (needs several L2 processors)
6. 024-stereo-field (needs DelayEngine, MidSide)
7. 025-tap-manager (needs multiple DelayEngines)

### Deferred Items

- **020-delay-crossfade**: Only implement if testing shows OnePoleSmoother isn't sufficient for time changes. May never be needed.
