<#
.SYNOPSIS
    Runs clang-tidy on the Krate Audio codebase.

.DESCRIPTION
    This script runs clang-tidy static analysis on the project source files.
    It uses the .clang-tidy configuration file in the project root.

    If compile_commands.json is available, it will be used for accurate flags.
    Otherwise, explicit include paths are used as a fallback.

    Files are analyzed in parallel by default for faster execution.

.PARAMETER BuildDir
    Path to the CMake build directory containing compile_commands.json.
    Default: build/windows-x64-release

.PARAMETER Fix
    Apply suggested fixes automatically. Use with caution and review changes.
    Forces sequential execution (-Jobs 1) to avoid concurrent edit conflicts.

.PARAMETER Target
    Limit analysis to a specific target:
      'dsp'       - DSP library headers + tests
      'dsp-tests' - DSP test files only
      'iterum'    - Iterum plugin source
      'disrumpo'  - Disrumpo plugin source
      'ruinae'    - Ruinae plugin source
      'innexus'   - Innexus plugin source
      'all'       - Everything
    Default: all

.PARAMETER Quiet
    Suppress progress output, only show warnings and errors.

.PARAMETER Jobs
    Number of parallel clang-tidy processes. Default: number of logical processors.
    Set to 1 for sequential execution.

.EXAMPLE
    .\tools\run-clang-tidy.ps1
    # Run clang-tidy on all source files

.EXAMPLE
    .\tools\run-clang-tidy.ps1 -Target dsp-tests -Jobs 8
    # Run on DSP tests with 8 parallel jobs

.EXAMPLE
    .\tools\run-clang-tidy.ps1 -Target dsp -Fix
    # Run on DSP library and apply fixes (sequential)

.EXAMPLE
    .\tools\run-clang-tidy.ps1 -BuildDir build/windows-x64-debug -Quiet
    # Use debug build and suppress progress output
#>

param(
    [string]$BuildDir = "build/windows-x64-release",
    [switch]$Fix,
    [ValidateSet("all", "dsp", "dsp-lib", "dsp-tests", "iterum", "disrumpo", "ruinae", "innexus", "gradus", "membrum", "seraphis")]
    [string]$Target = "all",
    [switch]$Quiet,
    [int]$Jobs = 0
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $ProjectRoot

# Colors for output
function Write-Info { param([string]$Message) Write-Host "[INFO] $Message" -ForegroundColor Cyan }
function Write-Success { param([string]$Message) Write-Host "[OK] $Message" -ForegroundColor Green }
function Write-Warn { param([string]$Message) Write-Host "[WARN] $Message" -ForegroundColor Yellow }
function Write-Err { param([string]$Message) Write-Host "[ERROR] $Message" -ForegroundColor Red }

# Find clang-tidy
$ClangTidyPath = $null
$ClangTidy = Get-Command clang-tidy -ErrorAction SilentlyContinue
if ($ClangTidy) {
    $ClangTidyPath = $ClangTidy.Source
} else {
    # Try common installation paths
    $PossiblePaths = @(
        "C:\Program Files\LLVM\bin\clang-tidy.exe",
        "C:\Program Files (x86)\LLVM\bin\clang-tidy.exe",
        "$env:LOCALAPPDATA\Programs\LLVM\bin\clang-tidy.exe"
    )
    foreach ($Path in $PossiblePaths) {
        if (Test-Path $Path) {
            $ClangTidyPath = $Path
            break
        }
    }
}

if (-not $ClangTidyPath) {
    Write-Err "clang-tidy not found. Please install LLVM/Clang tools."
    Write-Info "Install with: winget install LLVM.LLVM"
    Write-Info "Or download from: https://releases.llvm.org/"
    exit 1
}

if (-not $Quiet) {
    Write-Info "Using clang-tidy: $ClangTidyPath"
}

# Determine parallel job count
if ($Jobs -le 0) {
    $Jobs = [Environment]::ProcessorCount
}

# Force sequential when fixing to avoid concurrent file edits
if ($Fix -and $Jobs -gt 1) {
    Write-Warn "Fix mode: forcing sequential execution (-Jobs 1) to avoid conflicts"
    $Jobs = 1
}

if (-not $Quiet) {
    Write-Info "Parallel jobs: $Jobs"
}

# Check if compile_commands.json exists
$CompileCommands = Join-Path $BuildDir "compile_commands.json"
$UseCompileCommands = Test-Path $CompileCommands

if ($UseCompileCommands) {
    if (-not $Quiet) {
        Write-Info "Using compile_commands.json from: $BuildDir"
    }
} else {
    if (-not $Quiet) {
        Write-Warn "compile_commands.json not found. Using fallback include paths."
        Write-Info "For best results, configure with Ninja: cmake -G Ninja -B build/ninja ."
    }
}

# Determine source directories based on target
# $SourceDirs     - searched recursively for .cpp files
# $RootSourceDirs - searched non-recursively (e.g. dsp/ root for lint stub)
$SourceDirs = @()
$RootSourceDirs = @()
$IncludeDirs = @()

switch ($Target) {
    "dsp" {
        $SourceDirs += "dsp/include"
        $SourceDirs += "dsp/tests"
        $RootSourceDirs += "dsp"
        $IncludeDirs += "dsp/include"
    }
    "dsp-lib" {
        $SourceDirs += "dsp/include"
        $RootSourceDirs += "dsp"
        $IncludeDirs += "dsp/include"
    }
    "dsp-tests" {
        $SourceDirs += "dsp/tests"
        $IncludeDirs += "dsp/include"
    }
    "iterum" {
        $SourceDirs += "plugins/iterum/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/iterum/src"
        $IncludeDirs += "extern/vst3sdk"
    }
    "disrumpo" {
        $SourceDirs += "plugins/disrumpo/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/disrumpo/src"
        $IncludeDirs += "extern/vst3sdk"
    }
    "ruinae" {
        $SourceDirs += "plugins/ruinae/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/ruinae/src"
        $IncludeDirs += "extern/vst3sdk"
    }
    "innexus" {
        $SourceDirs += "plugins/innexus/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/innexus/src"
        $IncludeDirs += "extern/vst3sdk"
    }
    "gradus" {
        $SourceDirs += "plugins/gradus/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/gradus/src"
        $IncludeDirs += "extern/vst3sdk"
    }
    "membrum" {
        $SourceDirs += "plugins/membrum/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/membrum/src"
        $IncludeDirs += "extern/vst3sdk"
    }
    "seraphis" {
        # The tests tree is listed so this target matches run-clang-tidy.sh's
        # `seraphis)` case (tools/run-clang-tidy.sh:151-152), which has always
        # been ("plugins/seraphis/src" "plugins/seraphis/tests"). Without it the
        # two scripts analyze different file sets and a clean .ps1 run says
        # nothing about the Linux/macOS pre-commit lint - Seraphis spec
        # seraphis-phase10-effects SC-015 requires BOTH clean.
        $SourceDirs += "plugins/seraphis/src"
        $SourceDirs += "plugins/seraphis/tests"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/seraphis/src"
        $IncludeDirs += "plugins/seraphis/tests"
        $IncludeDirs += "tests"
        $IncludeDirs += "extern/vst3sdk"
    }
    "all" {
        $SourceDirs += "dsp/include"
        $SourceDirs += "dsp/tests"
        $RootSourceDirs += "dsp"
        $SourceDirs += "plugins/iterum/src"
        $SourceDirs += "plugins/disrumpo/src"
        $SourceDirs += "plugins/ruinae/src"
        $SourceDirs += "plugins/innexus/src"
        $SourceDirs += "plugins/gradus/src"
        $SourceDirs += "plugins/membrum/src"
        $SourceDirs += "plugins/seraphis/src"
        $IncludeDirs += "dsp/include"
        $IncludeDirs += "plugins/iterum/src"
        $IncludeDirs += "plugins/disrumpo/src"
        $IncludeDirs += "plugins/ruinae/src"
        $IncludeDirs += "plugins/innexus/src"
        $IncludeDirs += "plugins/gradus/src"
        $IncludeDirs += "plugins/membrum/src"
        $IncludeDirs += "plugins/seraphis/src"
        $IncludeDirs += "extern/vst3sdk"
    }
}

# Find all source files (only .cpp files for analysis, headers are checked via includes)
$SourceFiles = @()

# Recursive search in subdirectories
foreach ($Dir in $SourceDirs) {
    $FullPath = Join-Path $ProjectRoot $Dir
    if (Test-Path $FullPath) {
        $Files = Get-ChildItem -Path $FullPath -Recurse -Include "*.cpp" |
            Where-Object { $_.FullName -notmatch "extern|build|vst3sdk" }
        $SourceFiles += $Files
    }
}

# Non-recursive search in root directories (e.g. dsp/ for lint_all_headers.cpp)
foreach ($Dir in $RootSourceDirs) {
    $FullPath = Join-Path $ProjectRoot $Dir
    if (Test-Path $FullPath) {
        $Files = Get-ChildItem -Path $FullPath -Filter "*.cpp" |
            Where-Object { $_.FullName -notmatch "extern|build|vst3sdk" }
        $SourceFiles += $Files
    }
}

if ($SourceFiles.Count -eq 0) {
    Write-Warn "No source files found for target: $Target"
    exit 0
}

if (-not $Quiet) {
    Write-Info "Found $($SourceFiles.Count) source files to analyze"
}

# Build base clang-tidy arguments
# NOTE: Do NOT use --config-file here. It overrides the per-directory .clang-tidy
# lookup, which we need for test-specific overrides (e.g. dsp/tests/.clang-tidy).
# The root .clang-tidy is found automatically via directory walk.
$BaseArgs = @()

if ($UseCompileCommands) {
    $BaseArgs += "-p"
    $BaseArgs += $BuildDir
}

if ($Fix) {
    $BaseArgs += "-fix"
    Write-Warn "Fix mode enabled - files will be modified!"
}

# Build per-file argument lists
$FileJobs = @()
foreach ($File in $SourceFiles) {
    $RelativePath = $File.FullName.Replace($ProjectRoot + "\", "").Replace("\", "/")

    $TidyArgs = @() + $BaseArgs
    $TidyArgs += $File.FullName

    # Add fallback compiler flags if no compile_commands.json
    if (-not $UseCompileCommands) {
        $TidyArgs += "--"
        $TidyArgs += "-std=c++20"
        $TidyArgs += "-DWIN32"
        $TidyArgs += "-D_WIN32"
        $TidyArgs += "-DNOMINMAX"
        foreach ($Inc in $IncludeDirs) {
            $TidyArgs += "-I$Inc"
        }
    }

    $FileJobs += @{
        File         = $File
        RelativePath = $RelativePath
        Args         = $TidyArgs
    }
}

# Process clang-tidy output for a single file, applying project filters
function Process-TidyOutput {
    param(
        [string]$Output,
        [string]$RelativePath
    )

    if (-not $Output) {
        return @{ Warnings = 0; Errors = 0; Lines = @() }
    }

    # Filter to only our code paths (plugins/ and dsp/)
    $OurLines = $Output -split "`n" | Where-Object {
        ($_ -match "plugins[/\\]" -or $_ -match "dsp[/\\]") -and
        $_ -notmatch "Processing file" -and
        $_ -notmatch "Error while processing"
    }

    # Filter out known Catch2 macro false positives in test files.
    # Belt-and-suspenders with the per-directory .clang-tidy in dsp/tests/.
    $Catch2FalsePositives = "bugprone-chained-comparison|cppcoreguidelines-avoid-do-while"
    $OurLines = $OurLines | Where-Object {
        -not ($RelativePath -match "tests[/\\]" -and $_ -match $Catch2FalsePositives)
    }

    $warnings = @($OurLines | Where-Object { $_ -match "\bwarning:" }).Count
    $errors = @($OurLines | Where-Object { $_ -match "\berror:" -and $_ -notmatch "clang-diagnostic-error" }).Count

    $diagLines = @()
    if ($warnings -gt 0 -or $errors -gt 0) {
        $diagLines = @($OurLines | Where-Object { $_ -match "warning:|error:|note:" })
    }

    return @{
        Warnings = $warnings
        Errors   = $errors
        Lines    = $diagLines
    }
}

# ==============================================================================
# Parallel Execution
# ==============================================================================
$ErrorCount = 0
$WarningCount = 0
$FileCount = 0
$TempDir = Join-Path ([System.IO.Path]::GetTempPath()) "clang-tidy-$([guid]::NewGuid().ToString('N').Substring(0,8))"
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

try {
    $Running = [System.Collections.ArrayList]::new()
    $JobIndex = 0
    $TotalFiles = $FileJobs.Count

    while ($JobIndex -lt $TotalFiles -or $Running.Count -gt 0) {
        # Launch new processes up to the parallel limit
        while ($JobIndex -lt $TotalFiles -and $Running.Count -lt $Jobs) {
            $job = $FileJobs[$JobIndex]
            $outFile = Join-Path $TempDir "$JobIndex.out"
            $errFile = Join-Path $TempDir "$JobIndex.err"

            # Build argument string with proper quoting for paths with spaces
            $argStr = ($job.Args | ForEach-Object {
                if ($_ -match '\s') { "`"$_`"" } else { $_ }
            }) -join ' '

            $proc = Start-Process -FilePath $ClangTidyPath -ArgumentList $argStr `
                -NoNewWindow -PassThru `
                -RedirectStandardOutput $outFile `
                -RedirectStandardError $errFile

            [void]$Running.Add(@{
                Process      = $proc
                OutFile      = $outFile
                ErrFile      = $errFile
                RelativePath = $job.RelativePath
            })

            $JobIndex++
        }

        # Check for completed processes
        $stillRunning = [System.Collections.ArrayList]::new()

        foreach ($r in $Running) {
            if ($r.Process.HasExited) {
                # Read combined stdout + stderr
                $output = ""
                if (Test-Path $r.OutFile) {
                    $content = Get-Content $r.OutFile -Raw -ErrorAction SilentlyContinue
                    if ($content) { $output += $content }
                }
                if (Test-Path $r.ErrFile) {
                    $content = Get-Content $r.ErrFile -Raw -ErrorAction SilentlyContinue
                    if ($content) { $output += $content }
                }

                $result = Process-TidyOutput -Output $output -RelativePath $r.RelativePath

                $WarningCount += $result.Warnings
                $ErrorCount += $result.Errors
                $FileCount++

                # Print diagnostics for this file
                if ($result.Lines.Count -gt 0) {
                    Write-Host ""
                    foreach ($line in $result.Lines) {
                        Write-Host $line
                    }
                }

                if (-not $Quiet) {
                    Write-Host "`r[$FileCount/$TotalFiles] $($r.RelativePath)                    " -ForegroundColor Gray -NoNewline
                }
            } else {
                [void]$stillRunning.Add($r)
            }
        }
        $Running = $stillRunning

        # Avoid busy-waiting when all slots are occupied
        if ($Running.Count -gt 0 -and $Running.Count -ge $Jobs) {
            Start-Sleep -Milliseconds 100
        }
    }

    # Final newline after progress counter
    if (-not $Quiet) {
        Write-Host ""
    }

} finally {
    # Kill any still-running processes on Ctrl+C / error
    foreach ($r in $Running) {
        if ($r.Process -and -not $r.Process.HasExited) {
            try { $r.Process.Kill() } catch {}
        }
    }
    # Clean up temp directory
    Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
}

# Summary
Write-Host ""
Write-Host ("=" * 60)
Write-Info "Clang-Tidy Analysis Complete"
Write-Host "  Files analyzed: $FileCount"

if ($ErrorCount -gt 0) {
    Write-Err "  Errors: $ErrorCount"
} else {
    Write-Success "  Errors: 0"
}

if ($WarningCount -gt 0) {
    Write-Warn "  Warnings: $WarningCount"
} else {
    Write-Success "  Warnings: 0"
}

Write-Host ("=" * 60)

# Exit with error code if issues found
if ($ErrorCount -gt 0) {
    exit 1
}

exit 0
