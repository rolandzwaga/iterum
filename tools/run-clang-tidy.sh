#!/bin/bash
# ==============================================================================
# Run Clang-Tidy on Krate Audio Codebase
# ==============================================================================
# Usage:
#   ./tools/run-clang-tidy.sh              # Analyze all source files
#   ./tools/run-clang-tidy.sh --target dsp # Analyze DSP library only
#   ./tools/run-clang-tidy.sh --fix        # Apply automatic fixes
#   ./tools/run-clang-tidy.sh --help       # Show help
# ==============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

info() { echo -e "${CYAN}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Default values
BUILD_DIR=""
TARGET="all"
FIX=0
QUIET=0
PARALLEL=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --target)
            TARGET="$2"
            shift 2
            ;;
        --fix)
            FIX=1
            shift
            ;;
        --quiet)
            QUIET=1
            shift
            ;;
        --parallel)
            PARALLEL="$2"
            shift 2
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --build-dir DIR   Build directory with compile_commands.json"
            echo "                    (auto-detected if not specified)"
            echo "  --target TARGET   Target to analyze: all, dsp, shared, iterum, disrumpo,"
            echo "                    ruinae, innexus, gradus, membrum, seraphis"
            echo "                    ('all' covers dsp + every plugin). Default: all"
            echo "  --fix             Apply automatic fixes (use with caution)"
            echo "  --quiet           Suppress progress output"
            echo "  --parallel N      Number of parallel processes (default: $PARALLEL)"
            echo "  --help            Show this help message"
            exit 0
            ;;
        *)
            error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Find project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
cd "$PROJECT_ROOT"

# Auto-detect build directory if not specified
if [[ -z "$BUILD_DIR" ]]; then
    if [[ -f "build/linux-release/compile_commands.json" ]]; then
        BUILD_DIR="build/linux-release"
    elif [[ -f "build/linux-debug/compile_commands.json" ]]; then
        BUILD_DIR="build/linux-debug"
    elif [[ -f "build/macos-release/compile_commands.json" ]]; then
        BUILD_DIR="build/macos-release"
    elif [[ -f "build/macos-debug/compile_commands.json" ]]; then
        BUILD_DIR="build/macos-debug"
    elif [[ -f "build/compile_commands.json" ]]; then
        BUILD_DIR="build"
    else
        error "Could not find compile_commands.json"
        info "Run CMake first, e.g.: cmake --preset linux-release"
        exit 1
    fi
fi

# Check compile_commands.json exists
if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    error "compile_commands.json not found at: $BUILD_DIR/compile_commands.json"
    info "Run CMake first to generate it"
    exit 1
fi

# Find clang-tidy
CLANG_TIDY=$(command -v clang-tidy || true)
if [[ -z "$CLANG_TIDY" ]]; then
    error "clang-tidy not found. Please install LLVM/Clang tools."
    info "  Ubuntu/Debian: sudo apt install clang-tidy"
    info "  macOS: brew install llvm"
    info "  Arch: sudo pacman -S clang"
    exit 1
fi

if [[ $QUIET -eq 0 ]]; then
    info "Using clang-tidy: $CLANG_TIDY"
    info "Build directory: $BUILD_DIR"
fi

# Determine source directories
declare -a SOURCE_DIRS
case $TARGET in
    dsp)
        SOURCE_DIRS=("dsp/include" "dsp/tests")
        ;;
    shared)
        SOURCE_DIRS=("plugins/shared/src" "plugins/shared/tests")
        ;;
    iterum)
        SOURCE_DIRS=("plugins/iterum/src" "plugins/iterum/tests")
        ;;
    disrumpo)
        SOURCE_DIRS=("plugins/disrumpo/src" "plugins/disrumpo/tests")
        ;;
    ruinae)
        SOURCE_DIRS=("plugins/ruinae/src" "plugins/ruinae/tests")
        ;;
    innexus)
        SOURCE_DIRS=("plugins/innexus/src" "plugins/innexus/tests")
        ;;
    gradus)
        SOURCE_DIRS=("plugins/gradus/src" "plugins/gradus/tests")
        ;;
    membrum)
        SOURCE_DIRS=("plugins/membrum/src" "plugins/membrum/tests")
        ;;
    seraphis)
        SOURCE_DIRS=("plugins/seraphis/src" "plugins/seraphis/tests")
        ;;
    all)
        # MUST cover dsp + shared + EVERY plugin, or Linux/macOS pre-commit lint
        # silently skips code the .ps1 (and CI) would analyze. Keep in sync with
        # the plugin roster in run-clang-tidy.ps1 and trust plugins/ as the source.
        SOURCE_DIRS=(
            "dsp/include"
            "plugins/shared/src"
            "plugins/iterum/src"
            "plugins/disrumpo/src"
            "plugins/ruinae/src"
            "plugins/innexus/src"
            "plugins/gradus/src"
            "plugins/membrum/src"
            "plugins/seraphis/src"
        )
        ;;
    *)
        error "Unknown target: $TARGET"
        exit 1
        ;;
esac

# Find source files
SOURCE_FILES=()
for dir in "${SOURCE_DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        while IFS= read -r -d '' file; do
            SOURCE_FILES+=("$file")
        done < <(find "$dir" -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -print0 2>/dev/null)
    fi
done

if [[ ${#SOURCE_FILES[@]} -eq 0 ]]; then
    warn "No source files found for target: $TARGET"
    exit 0
fi

if [[ $QUIET -eq 0 ]]; then
    info "Found ${#SOURCE_FILES[@]} source files to analyze"
    info "Running with $PARALLEL parallel processes..."
fi

# Build clang-tidy arguments
TIDY_ARGS=("-p" "$BUILD_DIR" "--config-file=.clang-tidy")
if [[ $FIX -eq 1 ]]; then
    TIDY_ARGS+=("-fix")
    warn "Fix mode enabled - files will be modified!"
fi

# Run clang-tidy
ERROR_COUNT=0
WARNING_COUNT=0
FILE_COUNT=0
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

# Process files
for file in "${SOURCE_FILES[@]}"; do
    if [[ $QUIET -eq 0 ]]; then
        echo -e "\033[0;37mAnalyzing: $file\033[0m"
    fi

    OUTPUT=$("$CLANG_TIDY" "${TIDY_ARGS[@]}" "$file" 2>&1 || true)

    if [[ -n "$OUTPUT" ]]; then
        # Count warnings and errors
        WARNINGS=$(echo "$OUTPUT" | grep -c "warning:" || true)
        ERRORS=$(echo "$OUTPUT" | grep -c "error:" || true)

        WARNING_COUNT=$((WARNING_COUNT + WARNINGS))
        ERROR_COUNT=$((ERROR_COUNT + ERRORS))

        if [[ $WARNINGS -gt 0 || $ERRORS -gt 0 ]]; then
            echo "$OUTPUT"
        fi
    fi

    FILE_COUNT=$((FILE_COUNT + 1))
done

# Summary
echo ""
echo "============================================================"
info "Clang-Tidy Analysis Complete"
echo "  Files analyzed: $FILE_COUNT"

if [[ $ERROR_COUNT -gt 0 ]]; then
    error "  Errors: $ERROR_COUNT"
else
    success "  Errors: 0"
fi

if [[ $WARNING_COUNT -gt 0 ]]; then
    warn "  Warnings: $WARNING_COUNT"
else
    success "  Warnings: 0"
fi

echo "============================================================"

# Exit with error code if issues found
if [[ $ERROR_COUNT -gt 0 ]]; then
    exit 1
fi

exit 0
