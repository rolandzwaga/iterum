# Implementation Plan: [FEATURE]

**Branch**: `[###-feature-name]` | **Date**: [DATE] | **Spec**: [link]
**Input**: Feature specification from `/specs/[###-feature-name]/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/commands/plan.md` for the execution workflow.

## Summary

[Extract from feature spec: primary requirement + technical approach from research]

## Technical Context

<!--
  ACTION REQUIRED: Replace the content in this section with the technical details
  for the project. The structure here is presented in advisory capacity to guide
  the iteration process.
-->

**Language/Version**: [e.g., Python 3.11, Swift 5.9, Rust 1.75 or NEEDS CLARIFICATION]
**Primary Dependencies**: [e.g., FastAPI, UIKit, LLVM or NEEDS CLARIFICATION]
**Storage**: [if applicable, e.g., PostgreSQL, CoreData, files or N/A]
**Testing**: [e.g., pytest, XCTest, cargo test or NEEDS CLARIFICATION] *(Constitution Principle XII: Test-First Development)*
**Target Platform**: [e.g., Linux server, iOS 15+, WASM or NEEDS CLARIFICATION]
**Project Type**: [single/web/mobile - determines source structure]  
**Performance Goals**: [domain-specific, e.g., 1000 req/s, 10k lines/sec, 60 fps or NEEDS CLARIFICATION]  
**Constraints**: [domain-specific, e.g., <200ms p95, <100MB memory, offline-capable or NEEDS CLARIFICATION]  
**Scale/Scope**: [domain-specific, e.g., 10k users, 1M LOC, 50 screens or NEEDS CLARIFICATION]

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

[Gates determined based on constitution file]

**Required Check - Principle XII (Test-First Development):**
- [ ] Tasks will include TESTING-GUIDE.md context verification step
- [ ] Tests will be written BEFORE implementation code
- [ ] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [ ] Codebase Research section below is complete
- [ ] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

This section prevents One Definition Rule (ODR) violations by documenting existing components that may be reused or would conflict with new implementations.

### Mandatory Searches Performed

<!--
  ACTION REQUIRED: Before designing ANY new component, run these searches and document results.
  This is NON-NEGOTIABLE per Constitution Principle XIV.
-->

**Classes/Structs to be created**: [List planned new types]

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| [ClassName] | `grep -r "class ClassName" src/` | Yes/No | Reuse / Extend / Create New |
| [StructName] | `grep -r "struct StructName" src/` | Yes/No | Reuse / Extend / Create New |

**Utility Functions to be created**: [List planned new functions]

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| [functionName] | `grep -r "functionName" src/` | Yes/No | [file.h] | Reuse / Create New |

### Existing Components to Reuse

<!--
  List components from ARCHITECTURE.md or discovered via search that will be composed/reused.
  This prevents reinventing functionality and ensures proper layering.
-->

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| [e.g., dbToGain] | dsp/core/db_utils.h | 0 | [dB conversion in gain parameter] |
| [e.g., constexprExp] | dsp/core/db_utils.h | 0 | [coefficient calculation] |

### Files Checked for Conflicts

- [ ] `src/dsp/dsp_utils.h` - Legacy utilities (often contains simple implementations)
- [ ] `src/dsp/core/` - Layer 0 core utilities
- [ ] `ARCHITECTURE.md` - Component inventory
- [ ] [Other relevant files for this feature]

### ODR Risk Assessment

**Risk Level**: [Low / Medium / High]

**Justification**: [Why this risk level - e.g., "Low: All planned types are unique and not found in codebase" or "Medium: Similar utility exists in dsp_utils.h, will extend rather than duplicate"]

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

See CLAUDE.md "Layer 0 Refactoring Analysis" for decision framework.

### Utilities to Extract to Layer 0

<!--
  ACTION REQUIRED: Before implementation, identify any new utility functions that:
  1. Have audio-specific semantics worth documenting
  2. Will be needed by 3+ components
  3. Are pure, stateless functions that could benefit other features
-->

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| [e.g., stereoCrossBlend] | [Audio algorithm, reusable] | [stereo_utils.h] | [FeedbackNetwork, future ping-pong] |
| — | — | — | — |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| [e.g., msToSamples] | [One-liner, only 2 usages, class stores sampleRate_] |
| — | — |

**Decision**: [Summary of what will be extracted vs kept local]

## Project Structure

### Documentation (this feature)

```text
specs/[###-feature]/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/           # Phase 1 output (/speckit.plan command)
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)
<!--
  ACTION REQUIRED: Replace the placeholder tree below with the concrete layout
  for this feature. Delete unused options and expand the chosen structure with
  real paths (e.g., apps/admin, packages/something). The delivered plan must
  not include Option labels.
-->

```text
# [REMOVE IF UNUSED] Option 1: Single project (DEFAULT)
src/
├── models/
├── services/
├── cli/
└── lib/

tests/
├── contract/
├── integration/
└── unit/

# [REMOVE IF UNUSED] Option 2: Web application (when "frontend" + "backend" detected)
backend/
├── src/
│   ├── models/
│   ├── services/
│   └── api/
└── tests/

frontend/
├── src/
│   ├── components/
│   ├── pages/
│   └── services/
└── tests/

# [REMOVE IF UNUSED] Option 3: Mobile + API (when "iOS/Android" detected)
api/
└── [same as backend above]

ios/ or android/
└── [platform-specific structure: feature modules, UI flows, platform tests]
```

**Structure Decision**: [Document the selected structure and reference the real
directories captured above]

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| [e.g., 4th project] | [current need] | [why 3 projects insufficient] |
| [e.g., Repository pattern] | [specific problem] | [why direct DB access insufficient] |
