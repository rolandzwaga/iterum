# Research: Spectral Delay Tempo Sync

**Feature**: 041-spectral-tempo-sync
**Date**: 2025-12-31

## Summary

Minimal research required - this feature follows an established pattern used by 3 existing delay modes.

## Research Tasks

### 1. Tempo Sync Pattern

**Decision**: Follow granular_delay.h pattern exactly

**Rationale**:
- Digital Delay (026), PingPong Delay, and Granular Delay (038) all use the same tempo sync pattern
- Pattern is well-tested and proven to work
- Consistency across delay modes improves maintainability

**Alternatives considered**:
- Custom implementation: Rejected - would duplicate existing functionality
- Extract to shared base class: Rejected - overkill for a simple pattern, composition is cleaner

### 2. UI Visibility Pattern

**Decision**: Reuse VisibilityController class from controller.cpp

**Rationale**:
- Already works for Digital, PingPong, and Granular delay modes
- Handles IDependent registration and control visibility toggling
- Well-tested cross-platform solution

**Alternatives considered**:
- Custom visibility logic: Rejected - VisibilityController already handles all edge cases
- CSS-style visibility: Not available in VSTGUI

### 3. Parameter ID Allocation

**Decision**: Use IDs 211 (Time Mode) and 212 (Note Value)

**Rationale**:
- Spectral parameters use range 200-299
- IDs 200-210 are already allocated
- Sequential IDs maintain organization

### 4. TimeMode Enum Location

**Decision**: Import from dsp/systems/delay_engine.h

**Rationale**:
- TimeMode enum already exists and is used by multiple delay modes
- Reusing prevents ODR violations
- Maintains consistency across the codebase

## No Further Clarification Needed

All design decisions follow established patterns. No NEEDS CLARIFICATION markers remain in the specification.
