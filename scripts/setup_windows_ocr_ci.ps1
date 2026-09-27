#Requires -Version 5.1
<#
.SYNOPSIS
  Populate third_party/tesseract/windows for CI / headless builds.

.DESCRIPTION
  1. Skip immediately when the bundle is already complete (cache restore).
  2. Try VLMS_ocr_bundle.zip from the latest GitHub release (fast).
  3. MinGW: MSYS2 packages via fetch_windows_ocr.ps1
  4. MSVC: vcpkg (slow on first run; cached afterwards).
#>

param(
  [string]$VcpkgRoot = "",
  [string]$DestDir = "",
  [ValidateSet("Msvc", "MinGW")]
  [string]$Toolchain = "MinGW",
  [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "pe_dll_deps.ps1")

if (-not $DestDir) {
  $DestDir = Join-Path $RepoRoot "third_party\tesseract\windows"
}
if (-not $VcpkgRoot) {
  $VcpkgRoot = Join-Path $RepoRoot ".vcpkg"
}

function Test-OcrBundleComplete {
  param(
    [string]$Root,
    [string]$EffectiveToolchain
  )

  $required = @(
    (Join-Path $Root "include\tesseract\baseapi.h"),
    (Join-Path $Root "tessdata\eng.traineddata"),
    (Join-Path $Root "tessdata\ara.traineddata"),
    (Join-Path $Root "tessdata\fra.traineddata")
  )
  foreach ($path in $required) {
    if (-not (Test-Path $path)) { return $false }
  }

  $dlls = @(Get-ChildItem $Root -Filter *.dll -File -ErrorAction SilentlyContinue)
  if ($dlls.Count -eq 0) { return $false }

  if ($EffectiveToolchain -eq "MinGW") {
    $importLib = Get-ChildItem (Join-Path $Root "lib") -Filter "libtesseract*.dll.a" -File -ErrorAction SilentlyContinue |
      Select-Object -First 1
    if (-not $importLib) { return $false }

    if (-not (Test-PeImportsSatisfied -StageDir $Root)) {
      return $false
    }
  }

  return $true
}

function Expand-OcrBundleZip {
  param([string]$ZipPath, [string]$Dest)

  $temp = Join-Path $env:TEMP ("vlms_ocr_" + [guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Force -Path $temp | Out-Null
  try {
    Expand-Archive -Path $ZipPath -DestinationPath $temp -Force
    $root = Get-ChildItem $temp -Recurse -Directory -Filter "windows" -ErrorAction SilentlyContinue |
      Where-Object { $_.Parent.Name -eq "tesseract" } |
      Select-Object -First 1
    if (-not $root) {
      $root = Get-ChildItem $temp -Directory -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    if (-not $root) {
      throw "OCR zip has no extractable root directory."
    }

    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    Copy-Item (Join-Path $root.FullName "*") $Dest -Recurse -Force
  } finally {
    Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
  }
}

function Try-DownloadReleaseOcrBundle {
  param([string]$Dest)

  $ZipName = "VLMS_ocr_bundle.zip"
  $ZipPath = Join-Path $RepoRoot $ZipName

  if ($env:VLMS_OCR_BUNDLE_URL) {
    Write-Host "Downloading OCR bundle from VLMS_OCR_BUNDLE_URL..."
    Invoke-WebRequest -Uri $env:VLMS_OCR_BUNDLE_URL -OutFile $ZipPath
  } else {
    $remote = git -C $RepoRoot remote get-url origin 2>$null
    if (-not $remote -or $remote -notmatch 'github\.com[:/](?<owner>[^/]+)/(?<repo>[^/.]+)') {
      return $false
    }
    $repoSlug = "$($Matches.owner)/$($Matches.repo -replace '\.git$', '')"
    $headers = @{}
    if ($env:GITHUB_TOKEN) {
      $headers.Authorization = "Bearer $env:GITHUB_TOKEN"
    }

    try {
      $release = Invoke-RestMethod `
        -Uri "https://api.github.com/repos/$repoSlug/releases/latest" `
        -Headers $headers
    } catch {
      Write-Host "No latest release available for OCR bundle download."
      return $false
    }

    $asset = $release.assets | Where-Object { $_.name -eq $ZipName } | Select-Object -First 1
    if (-not $asset) { return $false }

    Write-Host "Downloading $ZipName from release $($release.tag_name)..."
    if ($env:GITHUB_TOKEN) {
      Invoke-WebRequest `
        -Uri $asset.url `
        -Headers @{ Authorization = "Bearer $env:GITHUB_TOKEN"; Accept = "application/octet-stream" } `
        -OutFile $ZipPath
    } else {
      Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $ZipPath
    }
  }

  Expand-OcrBundleZip -ZipPath $ZipPath -Dest $Dest
  Remove-Item $ZipPath -Force -ErrorAction SilentlyContinue
  return (Test-OcrBundleComplete -Root $Dest -EffectiveToolchain $Toolchain)
}

function Test-MingwOcrRoot {
  param([string]$Root)

  if (-not $Root -or -not (Test-Path $Root)) { return $false }
  $bin = Join-Path $Root "bin"
  return [bool](Get-ChildItem $bin -Filter "libtesseract-*.dll" -File -ErrorAction SilentlyContinue |
    Select-Object -First 1)
}

function Resolve-MingwOcrRoot {
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

  foreach ($root in ($candidates | Select-Object -Unique)) {
    if (Test-MingwOcrRoot -Root $root) {
      return $root
    }
  }
  return $null
}

function Setup-MinGwOcrFromMsys2 {
  param([string]$Dest)

  $mingwRoot = Resolve-MingwOcrRoot

  if (-not $mingwRoot) {
    throw @"
MSYS2 MinGW OCR packages not found.

Install in CI with msys2/setup-msys2:
  mingw-w64-x86_64-tesseract-ocr
  mingw-w64-x86_64-leptonica
  mingw-w64-x86_64-tesseract-data-eng
  mingw-w64-x86_64-tesseract-data-fra
  mingw-w64-x86_64-tesseract-data-ara
"@
  }

  Write-Host "Using MSYS2 MinGW OCR root: $mingwRoot"

  & (Join-Path $RepoRoot "scripts\fetch_windows_ocr.ps1") `
    -Toolchain MinGW `
    -SourceDir $mingwRoot
}

if (Test-OcrBundleComplete -Root $DestDir -EffectiveToolchain $Toolchain) {
  $dllCount = @(Get-ChildItem $DestDir -Filter *.dll -File).Count
  Write-Host "OCR bundle ready at $DestDir ($dllCount DLLs, $Toolchain)."
  exit 0
}

if ($Toolchain -eq "MinGW" -and (Test-Path $DestDir)) {
  $importLib = Get-ChildItem (Join-Path $DestDir "lib") -Filter "libtesseract*.dll.a" -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if (-not $importLib) {
    Write-Host "Removing stale non-MinGW OCR tree at $DestDir"
    Remove-Item $DestDir -Recurse -Force
  }
}

if ($VerifyOnly) {
  throw "OCR bundle incomplete at $DestDir"
}

New-Item -ItemType Directory -Force -Path $DestDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $DestDir "include") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $DestDir "lib") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $DestDir "tessdata") | Out-Null

Write-Host "Trying pre-built OCR release asset..."
if ($Toolchain -ne "MinGW" -and (Try-DownloadReleaseOcrBundle -Dest $DestDir)) {
  $dllCount = @(Get-ChildItem $DestDir -Filter *.dll -File).Count
  Write-Host "OCR bundle restored from release ($dllCount DLLs)."
  exit 0
}

if ($Toolchain -eq "MinGW") {
  Write-Host "Staging MinGW OCR from MSYS2 (skips MSVC release assets)..."
  Setup-MinGwOcrFromMsys2 -Dest $DestDir
  if (-not (Test-OcrBundleComplete -Root $DestDir -EffectiveToolchain $Toolchain)) {
    throw "MinGW OCR CI setup incomplete at $DestDir"
  }
  $dllCount = @(Get-ChildItem $DestDir -Filter *.dll -File).Count
  Write-Host "Windows MinGW OCR bundle ready at $DestDir ($dllCount DLLs)"
  exit 0
}

Write-Host "No OCR release asset — building via vcpkg (first run only, ~10–20 min)..."

if (-not (Test-Path (Join-Path $VcpkgRoot "vcpkg.exe"))) {
  Write-Host "Bootstrapping vcpkg in $VcpkgRoot"
  if (-not (Test-Path $VcpkgRoot)) {
    git clone --depth 1 https://github.com/microsoft/vcpkg.git $VcpkgRoot
  }
  Push-Location $VcpkgRoot
  .\bootstrap-vcpkg.bat -disableMetrics
  Pop-Location
}

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
if ($env:NUMBER_OF_PROCESSORS) {
  $env:VCPKG_MAX_CONCURRENCY = $env:NUMBER_OF_PROCESSORS
}

& $VcpkgExe install tesseract:x64-windows --triplet x64-windows

$Installed = Join-Path $VcpkgRoot "installed\x64-windows"
if (-not (Test-Path $Installed)) {
  throw "vcpkg install did not produce $Installed"
}

Copy-Item (Join-Path $Installed "include\*") (Join-Path $DestDir "include") -Recurse -Force
Get-ChildItem (Join-Path $Installed "lib") -Filter *.lib -File |
  Copy-Item -Destination (Join-Path $DestDir "lib") -Force
Get-ChildItem (Join-Path $Installed "bin") -Filter *.dll -File |
  Copy-Item -Destination $DestDir -Force

$TessdataRepo = "https://github.com/tesseract-ocr/tessdata/raw/main"
$Eng = Join-Path $Installed "share\tessdata\eng.traineddata"
if (Test-Path $Eng) {
  Copy-Item $Eng (Join-Path $DestDir "tessdata\eng.traineddata") -Force
} else {
  Invoke-WebRequest -Uri "$TessdataRepo/eng.traineddata" -OutFile (Join-Path $DestDir "tessdata\eng.traineddata")
}
foreach ($lang in @("ara", "fra")) {
  $out = Join-Path $DestDir "tessdata\$lang.traineddata"
  Write-Host "Downloading $lang.traineddata..."
  Invoke-WebRequest -Uri "$TessdataRepo/$lang.traineddata" -OutFile $out
}

if (-not (Test-OcrBundleComplete -Root $DestDir -EffectiveToolchain $Toolchain)) {
  throw "OCR CI setup incomplete at $DestDir"
}

$dllCount = @(Get-ChildItem $DestDir -Filter *.dll -File).Count
Write-Host "Windows OCR bundle ready at $DestDir ($dllCount DLLs)"
