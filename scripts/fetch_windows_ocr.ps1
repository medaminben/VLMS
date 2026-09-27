#Requires -Version 5.1
<#
.SYNOPSIS
  Copy Tesseract DLLs + tessdata (no CLI) into third_party/tesseract/windows.

.DESCRIPTION
  -Toolchain Msvc (default): UB Mannheim install (.lib + MSVC-built DLLs)
  -Toolchain MinGW: MSYS2 mingw64 packages (.dll.a import libs + MinGW DLLs)

  MinGW OCR DLLs still need libgcc/libstdc++/winpthread beside them; the installer
  build script copies those into the staged app folder automatically.
#>

param(
  [string]$SourceDir = "",
  [string]$DestDir = "",
  [ValidateSet("Msvc", "MinGW")]
  [string]$Toolchain = "Msvc"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $DestDir) {
  $DestDir = Join-Path $RepoRoot "third_party\tesseract\windows"
}

function Copy-TessLanguages {
  param(
    [string]$SrcTess,
    [string]$DestTess
  )

  foreach ($lang in @("ara", "fra", "eng")) {
    $src = Join-Path $SrcTess "$lang.traineddata"
    if (-not (Test-Path $src)) {
      throw "Missing $src — install the $lang language pack."
    }
    Copy-Item $src (Join-Path $DestTess "$lang.traineddata") -Force
  }
}

. (Join-Path $PSScriptRoot "pe_dll_deps.ps1")

function Copy-MinGwOcrBundle {
  param([string]$MingwRoot)

  if (-not $MingwRoot) {
    $candidates = @()
    if ($env:MSYS2_LOCATION) {
      $candidates += (Join-Path $env:MSYS2_LOCATION "mingw64")
    }
    if ($env:RUNNER_TEMP) {
      $candidates += (Join-Path $env:RUNNER_TEMP "setup-msys2\msys64\mingw64")
      $candidates += (Join-Path $env:RUNNER_TEMP "msys64\mingw64")
    }
    $candidates += @(
      "C:\msys64\mingw64",
      "C:\tools\msys64\mingw64",
      "D:\a\_temp\setup-msys2\msys64\mingw64",
      "D:\a\_temp\msys64\mingw64"
    )
    foreach ($cand in ($candidates | Select-Object -Unique)) {
      if (-not (Test-Path $cand)) { continue }
      $bin = Join-Path $cand "bin"
      if (Get-ChildItem $bin -Filter "libtesseract-*.dll" -File -ErrorAction SilentlyContinue | Select-Object -First 1) {
        $MingwRoot = $cand
        break
      }
    }
  }

  if (-not $MingwRoot -or -not (Test-Path $MingwRoot)) {
    throw @"
MSYS2 mingw64 root not found.

Install MSYS2 packages (in an MSYS2 MinGW64 shell):
  pacman -S --needed mingw-w64-x86_64-tesseract-data-eng \
                  mingw-w64-x86_64-tesseract-data-fra \
                  mingw-w64-x86_64-tesseract-data-ara \
                  mingw-w64-x86_64-tesseract-ocr \
                  mingw-w64-x86_64-leptonica

Then re-run:
  powershell -File scripts\fetch_windows_ocr.ps1 -Toolchain MinGW -SourceDir 'C:\msys64\mingw64'
"@
  }

  $bin = Join-Path $MingwRoot "bin"
  $lib = Join-Path $MingwRoot "lib"
  $shareTess = Join-Path $MingwRoot "share\tessdata"
  $include = Join-Path $MingwRoot "include"

  $tesseractDll = Get-ChildItem $bin -Filter "libtesseract-*.dll" -File -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -First 1
  if (-not $tesseractDll) {
    throw "libtesseract-*.dll not found under $bin"
  }

  $leptonicaDll = Get-ChildItem $bin -Filter "libleptonica-*.dll" -File -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending |
    Select-Object -First 1
  if (-not $leptonicaDll) {
    throw "libleptonica-*.dll not found under $bin"
  }

  $runtimeDir = Join-Path $DestDir "runtime"
  New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null

  $toCopy = Resolve-PeDllClosure -SeedPaths @($tesseractDll.FullName, $leptonicaDll.FullName) -SearchDir $bin
  if ($toCopy.Count -lt 2) {
    throw "Could not resolve MinGW OCR DLL dependencies. Install mingw-w64-x86_64-binutils and re-run."
  }

  foreach ($runtimeName in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    $runtimePath = Join-Path $bin $runtimeName
    if (Test-Path $runtimePath) {
      Copy-Item $runtimePath (Join-Path $runtimeDir $runtimeName) -Force
    }
  }

  foreach ($entry in $toCopy.GetEnumerator()) {
    Copy-Item $entry.Value (Join-Path $DestDir $entry.Key) -Force
  }

  Copy-TessLanguages -SrcTess $shareTess -DestTess (Join-Path $DestDir "tessdata")

  New-Item -ItemType Directory -Force -Path (Join-Path $DestDir "include") | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $DestDir "lib") | Out-Null

  if (Test-Path (Join-Path $include "tesseract")) {
    Copy-Item (Join-Path $include "tesseract") (Join-Path $DestDir "include\tesseract") -Recurse -Force
  }
  if (Test-Path (Join-Path $include "leptonica")) {
    Copy-Item (Join-Path $include "leptonica") (Join-Path $DestDir "include\leptonica") -Recurse -Force
  }

  Get-ChildItem $lib -Filter "libtesseract*.dll.a" -File -ErrorAction SilentlyContinue |
    Copy-Item -Destination (Join-Path $DestDir "lib") -Force
  Get-ChildItem $lib -Filter "libleptonica*.dll.a" -File -ErrorAction SilentlyContinue |
    Copy-Item -Destination (Join-Path $DestDir "lib") -Force

  Remove-Item (Join-Path $DestDir "tesseract.exe") -ErrorAction SilentlyContinue
}

Write-Host "Destination: $DestDir"
Write-Host "Toolchain:   $Toolchain"
New-Item -ItemType Directory -Force -Path (Join-Path $DestDir "tessdata") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $DestDir "include") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $DestDir "lib") | Out-Null

if ($Toolchain -eq "MinGW") {
  Copy-MinGwOcrBundle -MingwRoot $SourceDir
} elseif (-not $SourceDir) {
  Write-Host @"

Manual steps (MSVC / UB Mannheim — requires Visual C++ runtime on customer PCs unless rebuilt with MinGW):
  1. Install Tesseract (UB Mannheim): https://github.com/UB-Mannheim/tesseract/wiki
     Select Arabic, French, and English language data.
  2. Re-run:
       powershell -File scripts\fetch_windows_ocr.ps1 -SourceDir 'C:\Program Files\Tesseract-OCR'

For a self-contained installer without MSVC prerequisites, use MinGW instead:
  pacman -S mingw-w64-x86_64-tesseract-ocr mingw-w64-x86_64-leptonica ...
  powershell -File scripts\fetch_windows_ocr.ps1 -Toolchain MinGW

"@
  exit 0
} else {
  if (-not (Test-Path $SourceDir)) {
    throw "SourceDir not found: $SourceDir"
  }

  Get-ChildItem $SourceDir -Filter *.dll -File | Copy-Item -Destination $DestDir -Force
  if (Test-Path (Join-Path $SourceDir "bin")) {
    Get-ChildItem (Join-Path $SourceDir "bin") -Filter *.dll -File | Copy-Item -Destination $DestDir -Force
  }

  Copy-TessLanguages -SrcTess (Join-Path $SourceDir "tessdata") -DestTess (Join-Path $DestDir "tessdata")

  foreach ($incName in @("include", "tesseract", "leptonica")) {
    $cand = Join-Path $SourceDir $incName
    if (Test-Path $cand) {
      if ($incName -eq "include") {
        Copy-Item $cand (Join-Path $DestDir "include") -Recurse -Force
      } elseif ($incName -eq "tesseract" -and (Test-Path (Join-Path $cand "baseapi.h"))) {
        $target = Join-Path $DestDir "include\tesseract"
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item (Join-Path $cand "*") $target -Recurse -Force
      } elseif ($incName -eq "leptonica") {
        $target = Join-Path $DestDir "include\leptonica"
        New-Item -ItemType Directory -Force -Path $target | Out-Null
        Copy-Item (Join-Path $cand "*") $target -Recurse -Force
      }
    }
  }

  Get-ChildItem $SourceDir -Filter *.lib -File -ErrorAction SilentlyContinue |
    Copy-Item -Destination (Join-Path $DestDir "lib") -Force
  if (Test-Path (Join-Path $SourceDir "lib")) {
    Get-ChildItem (Join-Path $SourceDir "lib") -Filter *.lib -File -ErrorAction SilentlyContinue |
      Copy-Item -Destination (Join-Path $DestDir "lib") -Force
  }

  Remove-Item (Join-Path $DestDir "tesseract.exe") -ErrorAction SilentlyContinue
}

$required = @(
  (Join-Path $DestDir "tessdata\ara.traineddata"),
  (Join-Path $DestDir "tessdata\fra.traineddata"),
  (Join-Path $DestDir "tessdata\eng.traineddata")
)
$dlls = @(Get-ChildItem $DestDir -Filter *.dll -File -ErrorAction SilentlyContinue)
if ($dlls.Count -eq 0) {
  Write-Warning "No DLLs copied. Customers need tesseract*.dll + leptonica*.dll beside vlms.exe."
}

$missing = @($required | Where-Object { -not (Test-Path $_) })
if ($missing.Count -gt 0) {
  Write-Host "Bundle incomplete. Missing:"
  $missing | ForEach-Object { Write-Host "  $_" }
  exit 1
}

Write-Host "Windows OCR DLL bundle ready (no tesseract.exe, no system install)."
Write-Host "CMake ships *.dll + tessdata next to vlms.exe in the app folder only."
