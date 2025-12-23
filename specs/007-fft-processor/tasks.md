# Tasks: FFT Processor

**Input**: Design documents from `/specs/007-fft-processor/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/

**Tests**: This project follows TEST-FIRST methodology (Constitution Principle XII). Tests MUST be written before implementation.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

---

## User Story Summary

| Story | Priority | Description | Key Files |
|-------|----------|-------------|-----------|
| US1 | P1 | Forward FFT Analysis | fft.h, fft_test.cpp |
| US2 | P1 | Inverse FFT Synthesis | fft.h, fft_test.cpp |
| US3 | P1 | STFT with Windowing | stft.h, stft_test.cpp |
| US4 | P2 | Overlap-Add Reconstruction | stft.h, stft_test.cpp |
| US5 | P2 | Complex Spectrum Manipulation | spectral_buffer.h, spectral_buffer_test.cpp |
| US6 | P1 | Real-Time Safety | All files (verification) |

**Note**: US1 and US2 share the FFT class (forward/inverse are different methods). They are combined into Phase 3 but tasks are labeled by specific story.

---

## ⚠️ MANDATORY: Test-First Development Workflow

**CRITICAL**: Every implementation task MUST follow this workflow. These are not guidelines - they are requirements.

### Required Steps for EVERY Task

1. **Context Check**: Verify `specs/TESTING-GUIDE.md` is in context window. If not, READ IT FIRST.
2. **Write Failing Tests**: Create test file and write tests that FAIL (no implementation yet)
3. **Implement**: Write code to make tests pass
4. **Verify**: Run tests and confirm they pass
5. **Commit**: Commit the completed work

### Cross-Platform Compatibility Check

**CRITICAL for VST3 projects**: The VST3 SDK enables `-ffast-math` globally. After implementing tests that use NaN/infinity detection, add to `-fno-fast-math` list in `tests/CMakeLists.txt`.

---

## Phase 1: Setup ✅

**Purpose**: Create file structure and CMake integration

- [x] T001 Create src/dsp/core/window_functions.h with header guard and namespace skeleton
- [x] T002 Create src/dsp/primitives/fft.h with header guard and namespace skeleton
- [x] T003 Create src/dsp/primitives/spectral_buffer.h with header guard and namespace skeleton
- [x] T004 Create src/dsp/primitives/stft.h with header guard and namespace skeleton
- [x] T005 [P] Create tests/unit/core/window_functions_test.cpp with Catch2 includes
- [x] T006 [P] Create tests/unit/primitives/fft_test.cpp with Catch2 includes
- [x] T007 [P] Create tests/unit/primitives/spectral_buffer_test.cpp with Catch2 includes
- [x] T008 [P] Create tests/unit/primitives/stft_test.cpp with Catch2 includes
- [x] T009 Update tests/CMakeLists.txt to include new test files in dsp_tests target

**Checkpoint**: File structure ready for implementation ✅

---

## Phase 2: Foundational - Window Functions (Layer 0) ✅

**Purpose**: Window generators required by US1, US3, US4 - MUST complete before user stories

**⚠️ CRITICAL**: No FFT/STFT work can begin until window functions are complete

### 2.1 Pre-Implementation (MANDATORY)

- [x] T010 **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 2.2 Tests for Window Functions (Write FIRST - Must FAIL)

> **Constitution Principle XII**: Tests MUST be written and FAIL before implementation begins

- [x] T011 [P] Tests for besselI0() in tests/unit/core/window_functions_test.cpp (known values: I0(0)=1, I0(1)≈1.266, I0(3)≈4.881)
- [x] T012 [P] Tests for generateHann() in tests/unit/core/window_functions_test.cpp (endpoints, peak at center, COLA)
- [x] T013 [P] Tests for generateHamming() in tests/unit/core/window_functions_test.cpp (endpoints ~0.08, peak ~1.0)
- [x] T014 [P] Tests for generateBlackman() in tests/unit/core/window_functions_test.cpp (endpoints ~0, peak ~1.0)
- [x] T015 [P] Tests for generateKaiser() in tests/unit/core/window_functions_test.cpp (beta parameter effect)
- [x] T016 Tests for verifyCOLA() in tests/unit/core/window_functions_test.cpp (Hann@50%, Hann@75%, Hamming, Blackman)

### 2.3 Implementation for Window Functions

- [x] T017 Implement besselI0() using series expansion in src/dsp/core/window_functions.h
- [x] T018 [P] Implement generateHann() (periodic variant: 0.5 - 0.5*cos(2πn/N)) in src/dsp/core/window_functions.h
- [x] T019 [P] Implement generateHamming() (0.54 - 0.46*cos(2πn/N)) in src/dsp/core/window_functions.h
- [x] T020 [P] Implement generateBlackman() in src/dsp/core/window_functions.h
- [x] T021 Implement generateKaiser() using besselI0() in src/dsp/core/window_functions.h
- [x] T022 Implement verifyCOLA() helper function in src/dsp/core/window_functions.h
- [x] T023 Implement generate() factory function dispatching to specific generators
- [x] T024 Verify all window function tests pass

### 2.4 Cross-Platform Verification (MANDATORY)

- [x] T025 **Verify IEEE 754 compliance**: Check if window_functions_test.cpp uses std::isnan → add to -fno-fast-math list

### 2.5 Commit (MANDATORY)

- [ ] T026 **Commit completed Window Functions work** (Layer 0 foundation)

**Checkpoint**: Window functions ready - FFT implementation can now begin ✅

---

## Phase 3: US1+US2 - FFT Core (Forward & Inverse) 🎯 MVP ✅

**Goal**: Complete FFT class with forward and inverse transforms - the core building block

**Independent Test**: Transform known sine waves, verify round-trip reconstruction < 0.0001% error

### 3.1 Pre-Implementation (MANDATORY)

- [x] T027 [US1] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 3.2 Tests for Complex Struct (Write FIRST)

- [x] T028 [P] [US1] Tests for Complex struct arithmetic (+, -, *, conjugate) in tests/unit/primitives/fft_test.cpp
- [x] T029 [P] [US1] Tests for Complex magnitude() and phase() in tests/unit/primitives/fft_test.cpp

### 3.3 Implementation for Complex Struct

- [x] T030 [US1] Implement Complex POD struct with arithmetic operators in src/dsp/primitives/fft.h
- [x] T031 [US1] Implement Complex::magnitude() and phase() in src/dsp/primitives/fft.h
- [x] T032 [US1] Verify Complex struct tests pass

### 3.4 Tests for FFT Class (Write FIRST - Must FAIL)

- [x] T033 [P] [US1] Tests for FFT::prepare() (power-of-2 validation, LUT generation) in tests/unit/primitives/fft_test.cpp
- [x] T034 [P] [US1] Tests for FFT::forward() with DC signal (bin 0 only) in tests/unit/primitives/fft_test.cpp
- [x] T035 [P] [US1] Tests for FFT::forward() with sine wave at bin frequency in tests/unit/primitives/fft_test.cpp
- [x] T036 [P] [US1] Tests for FFT::forward() output format (N/2+1 bins, DC/Nyquist imag=0) in tests/unit/primitives/fft_test.cpp
- [x] T037 [P] [US2] Tests for FFT::inverse() basic reconstruction in tests/unit/primitives/fft_test.cpp
- [x] T038 [P] [US2] Tests for round-trip (forward→inverse) < 0.0001% error at sizes 256,512,1024,2048,4096 in tests/unit/primitives/fft_test.cpp

### 3.5 Implementation for FFT Class

- [x] T039 [US1] Implement bit-reversal LUT generation in FFT::prepare() in src/dsp/primitives/fft.h
- [x] T040 [US1] Implement twiddle factor precomputation (double→float) in FFT::prepare() in src/dsp/primitives/fft.h
- [x] T041 [US1] Implement Radix-2 DIT forward FFT algorithm in FFT::forward() in src/dsp/primitives/fft.h
- [x] T042 [US1] Implement real-to-complex packing (N/2+1 output bins) in FFT::forward() in src/dsp/primitives/fft.h
- [x] T043 [US1] Verify forward FFT tests pass (T033-T036)
- [x] T044 [US2] Implement complex-to-real unpacking for inverse in src/dsp/primitives/fft.h
- [x] T045 [US2] Implement Radix-2 DIT inverse FFT algorithm in FFT::inverse() in src/dsp/primitives/fft.h
- [x] T046 [US2] Implement FFT::reset() and query methods (size, numBins, isPrepared) in src/dsp/primitives/fft.h
- [x] T047 [US2] Verify all FFT tests pass including round-trip (T037-T038)

### 3.6 Cross-Platform Verification (MANDATORY)

- [x] T048 [US1] **Verify IEEE 754 compliance**: Check if fft_test.cpp uses std::isnan → add to -fno-fast-math list

### 3.7 Commit (MANDATORY)

- [ ] T049 [US1] [US2] **Commit completed FFT Core work** (US1 Forward + US2 Inverse)

**Checkpoint**: FFT forward/inverse complete and tested - ready for STFT and SpectralBuffer ✅

---

## Phase 4: US5 - Complex Spectrum Manipulation (P2) ✅

**Goal**: SpectralBuffer class for magnitude/phase access and manipulation

**Independent Test**: Set/get magnitude and phase at individual bins, verify Cartesian↔Polar conversion

### 4.1 Pre-Implementation (MANDATORY)

- [x] T050 [US5] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 4.2 Tests for SpectralBuffer (Write FIRST - Must FAIL)

- [x] T051 [P] [US5] Tests for SpectralBuffer::prepare() and numBins() in tests/unit/primitives/spectral_buffer_test.cpp
- [x] T052 [P] [US5] Tests for getMagnitude()/setMagnitude() in tests/unit/primitives/spectral_buffer_test.cpp
- [x] T053 [P] [US5] Tests for getPhase()/setPhase() in tests/unit/primitives/spectral_buffer_test.cpp
- [x] T054 [P] [US5] Tests for getReal()/getImag()/setCartesian() in tests/unit/primitives/spectral_buffer_test.cpp
- [x] T055 [P] [US5] Tests for reset() clears all bins in tests/unit/primitives/spectral_buffer_test.cpp
- [x] T056 [US5] Tests for Cartesian↔Polar round-trip accuracy in tests/unit/primitives/spectral_buffer_test.cpp

### 4.3 Implementation for SpectralBuffer

- [x] T057 [US5] Implement SpectralBuffer::prepare() and data storage in src/dsp/primitives/spectral_buffer.h
- [x] T058 [P] [US5] Implement getMagnitude()/setMagnitude() (sqrt(re²+im²), preserve angle) in src/dsp/primitives/spectral_buffer.h
- [x] T059 [P] [US5] Implement getPhase()/setPhase() (atan2, preserve magnitude) in src/dsp/primitives/spectral_buffer.h
- [x] T060 [P] [US5] Implement getReal()/getImag()/setCartesian() in src/dsp/primitives/spectral_buffer.h
- [x] T061 [US5] Implement reset() and query methods (numBins, isPrepared, data) in src/dsp/primitives/spectral_buffer.h
- [x] T062 [US5] Verify all SpectralBuffer tests pass

### 4.4 Cross-Platform Verification (MANDATORY)

- [x] T063 [US5] **Verify IEEE 754 compliance**: Check if spectral_buffer_test.cpp uses std::isnan → add to -fno-fast-math list

### 4.5 Commit (MANDATORY)

- [ ] T064 [US5] **Commit completed SpectralBuffer work**

**Checkpoint**: SpectralBuffer complete - magnitude/phase manipulation ready ✅

---

## Phase 5: US3 - STFT with Windowing (P1)

**Goal**: STFT class for continuous audio stream analysis with configurable windows

**Independent Test**: Process continuous signal, verify frame-by-frame spectral content matches windowed FFT

### 5.1 Pre-Implementation (MANDATORY)

- [ ] T065 [US3] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 5.2 Tests for STFT (Write FIRST - Must FAIL)

- [ ] T066 [P] [US3] Tests for STFT::prepare() with different window types in tests/unit/primitives/stft_test.cpp
- [ ] T067 [P] [US3] Tests for pushSamples()/canAnalyze() sample accumulation in tests/unit/primitives/stft_test.cpp
- [ ] T068 [P] [US3] Tests for analyze() applies window correctly (compare windowed FFT) in tests/unit/primitives/stft_test.cpp
- [ ] T069 [P] [US3] Tests for different hop sizes (50%, 75%) in tests/unit/primitives/stft_test.cpp
- [ ] T070 [US3] Tests for continuous streaming (multiple analyze calls) in tests/unit/primitives/stft_test.cpp

### 5.3 Implementation for STFT

- [ ] T071 [US3] Implement STFT::prepare() with FFT, window, and circular buffer allocation in src/dsp/primitives/stft.h
- [ ] T072 [US3] Implement STFT::pushSamples() circular buffer write in src/dsp/primitives/stft.h
- [ ] T073 [US3] Implement STFT::canAnalyze() threshold check in src/dsp/primitives/stft.h
- [ ] T074 [US3] Implement STFT::analyze() with frame extraction, windowing, and FFT in src/dsp/primitives/stft.h
- [ ] T075 [US3] Implement STFT::reset() and query methods in src/dsp/primitives/stft.h
- [ ] T076 [US3] Verify all STFT tests pass

### 5.4 Cross-Platform Verification (MANDATORY)

- [ ] T077 [US3] **Verify IEEE 754 compliance**: Check if stft_test.cpp uses std::isnan → add to -fno-fast-math list

### 5.5 Commit (MANDATORY)

- [ ] T078 [US3] **Commit completed STFT work**

**Checkpoint**: STFT analysis complete - ready for overlap-add synthesis

---

## Phase 6: US4 - Overlap-Add Reconstruction (P2)

**Goal**: OverlapAdd class for artifact-free reconstruction from spectral frames

**Independent Test**: STFT→ISTFT round-trip without modification achieves < 0.01% error (COLA verification)

### 6.1 Pre-Implementation (MANDATORY)

- [ ] T079 [US4] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 6.2 Tests for OverlapAdd (Write FIRST - Must FAIL)

- [ ] T080 [P] [US4] Tests for OverlapAdd::prepare() and accumulator allocation in tests/unit/primitives/stft_test.cpp
- [ ] T081 [P] [US4] Tests for synthesize() adds IFFT frame to accumulator in tests/unit/primitives/stft_test.cpp
- [ ] T082 [P] [US4] Tests for pullSamples()/samplesAvailable() output extraction in tests/unit/primitives/stft_test.cpp
- [ ] T083 [US4] Tests for STFT→ISTFT round-trip < 0.01% error (Hann 50%) in tests/unit/primitives/stft_test.cpp
- [ ] T084 [US4] Tests for STFT→ISTFT round-trip < 0.01% error (Hann 75%) in tests/unit/primitives/stft_test.cpp
- [ ] T085 [US4] Tests for COLA property verification with different windows in tests/unit/primitives/stft_test.cpp
- [ ] T085b [US4] Tests for Kaiser window COLA at 90% overlap (required per data-model.md) in tests/unit/primitives/stft_test.cpp

### 6.3 Implementation for OverlapAdd

- [ ] T086 [US4] Implement OverlapAdd::prepare() with IFFT and output accumulator in src/dsp/primitives/stft.h
- [ ] T087 [US4] Implement OverlapAdd::synthesize() with IFFT and accumulator addition in src/dsp/primitives/stft.h
- [ ] T088 [US4] Implement OverlapAdd::pullSamples() output extraction and buffer shift in src/dsp/primitives/stft.h
- [ ] T089 [US4] Implement OverlapAdd::reset() and query methods in src/dsp/primitives/stft.h
- [ ] T090 [US4] Verify all OverlapAdd tests pass including COLA round-trip

### 6.4 Cross-Platform Verification (MANDATORY)

- [ ] T091 [US4] **Verify IEEE 754 compliance**: Check if stft_test.cpp (OLA section) uses std::isnan → already handled in T077

### 6.5 Commit (MANDATORY)

- [ ] T092 [US4] **Commit completed OverlapAdd work**

**Checkpoint**: STFT+OLA complete - full spectral processing pipeline functional

---

## Phase 7: US6 - Real-Time Safety Verification (P1)

**Goal**: Verify all components meet real-time constraints (noexcept, no allocations in process)

**Independent Test**: Code inspection and static analysis verify real-time safety

### 7.1 Pre-Implementation (MANDATORY)

- [ ] T093 [US6] **Verify TESTING-GUIDE.md is in context** (ingest `specs/TESTING-GUIDE.md` if needed)

### 7.2 Tests for Real-Time Safety (Write FIRST)

- [ ] T094 [US6] Tests verifying FFT::forward()/inverse() marked noexcept in tests/unit/primitives/fft_test.cpp
- [ ] T095 [US6] Tests verifying STFT::pushSamples()/analyze() marked noexcept in tests/unit/primitives/stft_test.cpp
- [ ] T096 [US6] Tests verifying OverlapAdd::synthesize()/pullSamples() marked noexcept in tests/unit/primitives/stft_test.cpp
- [ ] T097 [US6] Tests verifying SpectralBuffer accessors marked noexcept in tests/unit/primitives/spectral_buffer_test.cpp

### 7.3 Verification

- [ ] T098 [US6] Code review: Verify no new/delete/malloc/free in process methods
- [ ] T099 [US6] Code review: Verify all buffers pre-allocated in prepare() methods
- [ ] T100 [US6] Code review: Verify noexcept on all process methods per contracts/fft_processor.h
- [ ] T100b [US6] Verify memory footprint ≤ 3*FFT_SIZE*sizeof(float) per NFR-003 in tests/unit/primitives/fft_test.cpp
- [ ] T101 [US6] Verify all noexcept tests pass

### 7.4 Commit (MANDATORY)

- [ ] T102 [US6] **Commit real-time safety verification work**

**Checkpoint**: All components verified real-time safe

---

## Phase 8: Polish & Integration

**Purpose**: Final integration testing and documentation

- [ ] T103 Integration test: FFT→SpectralBuffer manipulation→IFFT round-trip
- [ ] T104 Integration test: Full STFT→modify spectrum→OLA pipeline
- [ ] T105 Performance test: Verify O(N log N) FFT scaling at multiple sizes
- [ ] T106 Run all tests to verify complete integration

### 8.1 Commit (MANDATORY)

- [ ] T107 **Commit integration tests**

---

## Phase 9: Final Documentation (MANDATORY)

**Purpose**: Update living architecture documentation before spec completion

> **Constitution Principle XIII**: Every spec implementation MUST update ARCHITECTURE.md as a final task

### 9.1 Architecture Documentation Update

- [ ] T108 **Update ARCHITECTURE.md** with new Layer 0 components:
  - Window::generateHann(), generateHamming(), generateBlackman(), generateKaiser()
  - Window::verifyCOLA(), besselI0()
- [ ] T109 **Update ARCHITECTURE.md** with new Layer 1 components:
  - Complex struct
  - FFT class (forward/inverse real FFT)
  - SpectralBuffer class (magnitude/phase manipulation)
  - STFT class (streaming analysis)
  - OverlapAdd class (overlap-add synthesis)

### 9.2 Final Commit

- [ ] T110 **Commit ARCHITECTURE.md updates**
- [ ] T111 Verify all spec work is committed to feature branch

**Checkpoint**: Spec implementation complete - ARCHITECTURE.md reflects all new functionality

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
    │
    ▼
Phase 2 (Window Functions) ──BLOCKS ALL──▶ Phases 3-6
    │
    ▼
Phase 3 (FFT Core) ──────────────────────▶ Required by Phase 5, 6
    │
    ├───────────────┐
    ▼               ▼
Phase 4         Phase 5
(SpectralBuffer) (STFT) ─────────────────▶ Required by Phase 6
(Independent)      │
    │              │
    │              ▼
    │           Phase 6 (OverlapAdd)
    │              │
    └──────┬───────┘
           ▼
       Phase 7 (Real-Time Safety)
           │
           ▼
       Phase 8 (Integration)
           │
           ▼
       Phase 9 (Documentation)
```

### User Story Dependencies

| Story | Depends On | Can Parallel With |
|-------|------------|-------------------|
| US1 (Forward FFT) | Phase 2 (Windows) | - |
| US2 (Inverse FFT) | US1 (FFT class) | - |
| US3 (STFT) | US1+US2 (FFT), Phase 2 | US5 |
| US4 (OverlapAdd) | US2 (Inverse FFT), US3 | US5 |
| US5 (SpectralBuffer) | Phase 3 (Complex) | US3, US4 |
| US6 (Real-Time) | All stories complete | - |

### Parallel Opportunities

**Within Phase 2 (Windows)**:
```
T011, T012, T013, T014, T015 can run in parallel
T018, T019, T020 can run in parallel
```

**Within Phase 3 (FFT)**:
```
T028, T029 can run in parallel
T033, T034, T035, T036, T037, T038 can run in parallel
```

**Phase 4 (SpectralBuffer) can run in parallel with Phase 5 (STFT)** after Phase 3 completes

**Within Phase 4 (SpectralBuffer)**:
```
T051-T055 can run in parallel
T058, T059, T060 can run in parallel
```

---

## Implementation Strategy

### MVP First (US1 + US2 Only)

1. Complete Phase 1: Setup
2. Complete Phase 2: Window Functions
3. Complete Phase 3: FFT Core (US1 + US2)
4. **STOP and VALIDATE**: Test FFT forward/inverse independently
5. Demo basic spectral analysis capability

### Full Feature Delivery

1. Setup + Windows → Foundation ready
2. FFT Core (US1+US2) → Basic spectral transforms ✓
3. SpectralBuffer (US5) → Spectrum manipulation ✓
4. STFT (US3) → Streaming analysis ✓
5. OverlapAdd (US4) → Complete round-trip ✓
6. Real-Time Safety (US6) → Production ready ✓
7. Documentation → Spec complete ✓

---

## Task Summary

| Phase | Tasks | Parallel Tasks |
|-------|-------|----------------|
| 1. Setup | T001-T009 (9) | T005-T008 (4) |
| 2. Windows | T010-T026 (17) | T011-T015, T018-T020 (8) |
| 3. FFT Core | T027-T049 (23) | T028-T029, T033-T038 (8) |
| 4. SpectralBuffer | T050-T064 (15) | T051-T055, T058-T060 (8) |
| 5. STFT | T065-T078 (14) | T066-T069 (4) |
| 6. OverlapAdd | T079-T092 + T085b (15) | T080-T082 (3) |
| 7. Real-Time | T093-T102 + T100b (11) | T094-T097 (4) |
| 8. Integration | T103-T107 (5) | - |
| 9. Documentation | T108-T111 (4) | - |
| **Total** | **113 tasks** | **39 parallelizable** |

---

## Notes

- [P] tasks = different files, no dependencies
- [US#] label maps task to specific user story for traceability
- **MANDATORY**: Check TESTING-GUIDE.md is in context FIRST
- **MANDATORY**: Write tests that FAIL before implementing (Principle XII)
- **MANDATORY**: Verify IEEE 754 functions have `-fno-fast-math` in tests/CMakeLists.txt
- **MANDATORY**: Commit work at end of each user story
- **MANDATORY**: Update ARCHITECTURE.md before spec completion (Principle XIII)
- Stop at any checkpoint to validate story independently
