#!/usr/bin/env pwsh
# Personal Finance Hub - Local Quality Check Script
# Version: 1.0
# This script runs all quality checks before committing

$ErrorActionPreference = "Stop"

Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Personal Finance Hub - Quality Check" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host ""

$failedChecks = @()

# =============================================================================
# 1. Git Whitespace Check
# =============================================================================
Write-Host "[1/5] Checking for whitespace errors..." -ForegroundColor Yellow
try {
    git diff --check
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ No whitespace errors found" -ForegroundColor Green
    } else {
        Write-Host "✗ Whitespace errors detected" -ForegroundColor Red
        $failedChecks += "Git whitespace check"
    }
} catch {
    Write-Host "✗ Git check failed: $_" -ForegroundColor Red
    $failedChecks += "Git whitespace check"
}
Write-Host ""

# =============================================================================
# 2. CMake Configure
# =============================================================================
Write-Host "[2/5] Running CMake configure..." -ForegroundColor Yellow
if (Test-Path "build") {
    Remove-Item -Recurse -Force "build"
}
New-Item -ItemType Directory -Path "build" | Out-Null

try {
    Push-Location "build"
    cmake .. -DCMAKE_BUILD_TYPE=Debug
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ CMake configure passed" -ForegroundColor Green
    } else {
        Write-Host "✗ CMake configure failed" -ForegroundColor Red
        $failedChecks += "CMake configure"
    }
    Pop-Location
} catch {
    Write-Host "✗ CMake configure failed: $_" -ForegroundColor Red
    $failedChecks += "CMake configure"
    Pop-Location
}
Write-Host ""

# =============================================================================
# 3. Build
# =============================================================================
Write-Host "[3/5] Building project..." -ForegroundColor Yellow
try {
    Push-Location "build"
    cmake --build . --config Debug
    if ($LASTEXITCODE -eq 0) {
        Write-Host "✓ Build passed" -ForegroundColor Green
    } else {
        Write-Host "✗ Build failed" -ForegroundColor Red
        $failedChecks += "Build"
    }
    Pop-Location
} catch {
    Write-Host "✗ Build failed: $_" -ForegroundColor Red
    $failedChecks += "Build"
    Pop-Location
}
Write-Host ""

# =============================================================================
# 4. Unit Tests (when available)
# =============================================================================
Write-Host "[4/5] Running unit tests..." -ForegroundColor Yellow
# TODO: Uncomment when unit tests are implemented
# try {
#     Push-Location "build"
#     ctest -C Debug --output-on-failure
#     if ($LASTEXITCODE -eq 0) {
#         Write-Host "✓ Unit tests passed" -ForegroundColor Green
#     } else {
#         Write-Host "✗ Unit tests failed" -ForegroundColor Red
#         $failedChecks += "Unit tests"
#     }
#     Pop-Location
# } catch {
#     Write-Host "✗ Unit tests failed: $_" -ForegroundColor Red
#     $failedChecks += "Unit tests"
#     Pop-Location
# }
Write-Host "⊘ Unit tests not yet implemented (will enable when tests are added)" -ForegroundColor Gray
Write-Host ""

# =============================================================================
# 5. Markdown Check
# =============================================================================
Write-Host "[5/5] Checking Markdown files..." -ForegroundColor Yellow
# Basic markdown checks - can be enhanced with markdownlint if available
$mdFiles = Get-ChildItem -Recurse -Filter "*.md" -Exclude "node_modules","build"
$mdIssues = 0
foreach ($file in $mdFiles) {
    $content = Get-Content $file.FullName -Raw
    # Check for trailing whitespace on non-empty lines
    $lines = Get-Content $file.FullName
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '\s+$' -and $lines[$i] -ne "") {
            Write-Host "  Warning: Trailing whitespace in $($file.Name):$($i+1)" -ForegroundColor Yellow
            $mdIssues++
        }
    }
}
if ($mdIssues -eq 0) {
    Write-Host "✓ Markdown files look good" -ForegroundColor Green
} else {
    Write-Host "⚠ Found $mdIssues markdown issues (non-blocking)" -ForegroundColor Yellow
}
Write-Host ""

# =============================================================================
# Summary
# =============================================================================
Write-Host "=====================================================" -ForegroundColor Cyan
if ($failedChecks.Count -eq 0) {
    Write-Host "✓ All quality checks passed!" -ForegroundColor Green
    Write-Host "=====================================================" -ForegroundColor Cyan
    exit 0
} else {
    Write-Host "✗ Quality checks failed:" -ForegroundColor Red
    foreach ($check in $failedChecks) {
        Write-Host "  - $check" -ForegroundColor Red
    }
    Write-Host "=====================================================" -ForegroundColor Cyan
    exit 1
}
