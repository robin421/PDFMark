<#
.SYNOPSIS
    PDFMark Windows Packaging Script
    Packages PdfMark executable with Qt 6 runtime dependencies, PDFium binary, and produces a standalone ZIP.

.PARAMETER BuildDir
    The CMake build directory (default: 'build' or 'build/Release')

.PARAMETER OutputDir
    The destination directory for the packaged release (default: 'dist')

.PARAMETER QtDir
    Root directory of the Qt 6 installation containing bin/windeployqt.exe
#>

param (
    [string]$BuildDir = "build",
    [string]$OutputDir = "dist",
    [string]$QtDir = ""
)

$ErrorActionPreference = "Stop"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host " PDFMark Windows Release Packaging Tool" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. Locate Executable
$exeCandidates = @(
    "$BuildDir/Release/PdfMark.exe",
    "$BuildDir/PdfMark.exe",
    "$BuildDir/bin/PdfMark.exe"
)

$exePath = $null
foreach ($cand in $exeCandidates) {
    if (Test-Path $cand) {
        $exePath = (Resolve-Path $cand).Path
        break
    }
}

if (-not $exePath) {
    Write-Error "Could not find PdfMark.exe in any expected location ($($exeCandidates -join ', '))"
    exit 1
}

Write-Host "[1/5] Found executable: $exePath" -ForegroundColor Green

# 2. Locate windeployqt
$windeployqt = $null
if ($QtDir -and (Test-Path "$QtDir/bin/windeployqt.exe")) {
    $windeployqt = "$QtDir/bin/windeployqt.exe"
} else {
    $found = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($found) {
        $windeployqt = $found.Source
    }
}

if (-not $windeployqt) {
    Write-Error "windeployqt.exe not found. Please provide -QtDir or add Qt bin to PATH."
    exit 1
}

Write-Host "[2/5] Found windeployqt: $windeployqt" -ForegroundColor Green

# 3. Prepare Staging Directory
$packageDir = Join-Path $OutputDir "PDFMark-Windows-x64"
if (Test-Path $packageDir) {
    Remove-Item -Recurse -Force $packageDir
}
New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

# Copy main binary
Copy-Item $exePath -Destination $packageDir

# 4. Locate and Copy pdfium.dll
$pdfiumCandidates = @(
    "$BuildDir/pdfium.dll",
    "$BuildDir/Release/pdfium.dll",
    "$BuildDir/_deps/pdfium-src/bin/pdfium.dll",
    "$BuildDir/_deps/pdfium-src/lib/pdfium.dll",
    "third_party/pdfium/bin/pdfium.dll"
)

$pdfiumPath = $null
foreach ($cand in $pdfiumCandidates) {
    if (Test-Path $cand) {
        $pdfiumPath = (Resolve-Path $cand).Path
        break
    }
}

if ($pdfiumPath) {
    Write-Host "[3/5] Found pdfium.dll: $pdfiumPath" -ForegroundColor Green
    Copy-Item $pdfiumPath -Destination $packageDir
} else {
    Write-Warning "pdfium.dll not found in standard build locations. Ensure it is placed adjacent to PdfMark.exe manually if dynamically linked."
}

# 5. Run windeployqt
Write-Host "[4/5] Deploying Qt dependencies..." -ForegroundColor Yellow
$deployTarget = Join-Path $packageDir "PdfMark.exe"
& $windeployqt --release --no-translations --compiler-runtime --no-opengl-sw $deployTarget

if ($LASTEXITCODE -ne 0) {
    Write-Error "windeployqt failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

# 6. Copy License and Readme if present
if (Test-Path "README.md") {
    Copy-Item "README.md" -Destination $packageDir
}
if (Test-Path "LICENSE") {
    Copy-Item "LICENSE" -Destination $packageDir
}

# 7. Create Zip Archive
$zipName = "PDFMark-Windows-x64.zip"
$zipPath = Join-Path $OutputDir $zipName
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}

Write-Host "[5/5] Creating archive: $zipPath..." -ForegroundColor Yellow
Compress-Archive -Path "$packageDir/*" -DestinationPath $zipPath -Force

$hash = (Get-FileHash -Algorithm SHA256 $zipPath).Hash
Write-Host "=========================================" -ForegroundColor Green
Write-Host " Packaging Complete!" -ForegroundColor Green
Write-Host " Output: $zipPath" -ForegroundColor Green
Write-Host " SHA256: $hash" -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Green
