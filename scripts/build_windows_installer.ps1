#Requires -Version 5.1
<#
.SYNOPSIS
  Build a self-contained VLMS Windows installer (single .exe, one app folder).

.DESCRIPTION
  1. Configure + build Release (VLMS_DEV_PATHS=OFF)
  2. cmake --install into build/installer/stage
  3. windeployqt (Qt DLLs; MSVC runtime only when using an MSVC Qt kit)
  4. Inno Setup compiles dist/VLMS_Setup_<version>.exe

  Recommended for customer machines (no Visual C++ Redistributable):
    -Toolchain MinGW with a Qt mingw_64 kit, e.g. C:\Qt\6.8.0\mingw_64
    - Populate OCR with MinGW DLLs: scripts\fetch_windows_ocr.ps1 -Toolchain MinGW

  Prerequisites on the Windows build PC:
    - CMake 3.21+, Qt 6 (Core, Widgets), matching compiler (MinGW or MSVC)
    - MSYS2 mingw-w64-x86_64-sqlite3 (MinGW: Core links the C API, not Qt Sql)
    - Inno Setup 6 (ISCC.exe on PATH or -InnoSetupDir)
    - third_party/tesseract/windows populated

.EXAMPLE
  powershell -File scripts\build_windows_installer.ps1 -Toolchain MinGW -QtDir 'C:\Qt\6.8.0\mingw_64'
  powershell -File scripts\build_windows_installer.ps1 -Toolchain Msvc -QtDir 'C:\Qt\6.8.0\msvc2022_64'
#>

param(
  [string]$BuildDir = "",
  [string]$QtDir = "",
  [string]$InnoSetupDir = "",
  [string]$Configuration = "Release",
  [ValidateSet("Auto", "Msvc", "MinGW")]
  [string]$Toolchain = "MinGW",
  [switch]$RequireInstallerData,
  [switch]$SkipBuild,
  [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "pe_dll_deps.ps1")

if (-not $BuildDir) {
  $BuildDir = Join-Path $RepoRoot "build"
}

$StageDir = Join-Path $BuildDir "installer\stage"
$DistDir = Join-Path $RepoRoot "dist"
$IssFile = Join-Path $RepoRoot "installer\vlms.iss"

function Resolve-QtRoot {
  param([string]$Hint)

  if ($Hint) {
    if (Test-Path (Join-Path $Hint "bin\windeployqt.exe")) {
      return (Resolve-Path $Hint).Path
    }
    throw "windeployqt.exe not found under $(Join-Path $Hint 'bin')"
  }

  foreach ($envName in @("QTDIR", "QT_ROOT_DIR")) {
    $envRoot = [Environment]::GetEnvironmentVariable($envName)
    if ($envRoot) {
      return (Resolve-QtRoot -Hint $envRoot)
    }
  }

  $candidates = if ($Toolchain -eq "MinGW") {
    @(
      "C:\Qt\6.8.1\mingw_64",
      "C:\Qt\6.8.0\mingw_64",
      "C:\Qt\6.7.3\mingw_64",
      "C:\Qt\6.6.3\mingw_64"
    )
  } else {
    @(
      "C:\Qt\6.8.1\msvc2022_64",
      "C:\Qt\6.8.0\msvc2022_64",
      "C:\Qt\6.7.3\msvc2022_64",
      "C:\Qt\6.6.3\msvc2022_64"
    )
  }
  foreach ($root in $candidates) {
    if (Test-Path (Join-Path $root "bin\windeployqt.exe")) {
      Write-Host "Using Qt from $root"
      return (Resolve-Path $root).Path
    }
  }

  throw "Set -QtDir or QTDIR to your Qt 6 installation (needs bin\windeployqt.exe)."
}

function Get-QtToolchain {
  param([string]$QtRoot)

  if ($QtRoot -match '(?i)mingw') { return "MinGW" }
  if ($QtRoot -match '(?i)msvc') { return "Msvc" }
  return "Unknown"
}

function Resolve-MinGwBinDir {
  param([string]$QtRoot)

  # Qt layout: <QtInstall>/6.x.x/mingw_64 and <QtInstall>/Tools/mingw1310_64/bin
  $qtVersionDir = Split-Path -Parent $QtRoot
  $qtInstallRoot = Split-Path -Parent $qtVersionDir
  $toolsRoot = Join-Path $qtInstallRoot "Tools"

  $toolRoots = @(
    (Join-Path $toolsRoot "mingw1310_64\bin"),
    (Join-Path $toolsRoot "mingw1120_64\bin"),
    (Join-Path $toolsRoot "mingw810_64\bin"),
    "C:\msys64\mingw64\bin"
  )

  foreach ($bin in $toolRoots) {
    $gpp = Join-Path $bin "g++.exe"
    if (Test-Path $gpp) {
      return (Resolve-Path $bin).Path
    }
  }

  $pathGpp = Get-Command g++.exe -ErrorAction SilentlyContinue
  if ($pathGpp) {
    return (Split-Path -Parent $pathGpp.Source)
  }

  throw @"
MinGW g++ not found. Install Qt's MinGW component or MSYS2 mingw-w64-gcc, or add g++.exe to PATH.
Qt root: $QtRoot
"@
}

function Resolve-Iscc {
  param([string]$Hint)

  if ($Hint) {
    $iscc = Join-Path $Hint "ISCC.exe"
    if (-not (Test-Path $iscc)) { throw "ISCC.exe not found: $iscc" }
    return $iscc
  }

  $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $default = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
  if (Test-Path $default) { return $default }

  throw "Inno Setup 6 not found. Install from https://jrsoftware.org/isinfo.php or pass -InnoSetupDir."
}

function Test-OcrBundle {
  param([string]$EffectiveToolchain)

  $ocrRoot = Join-Path $RepoRoot "third_party\tesseract\windows"
  $required = @(
    (Join-Path $ocrRoot "tessdata\eng.traineddata"),
    (Join-Path $ocrRoot "tessdata\ara.traineddata"),
    (Join-Path $ocrRoot "tessdata\fra.traineddata")
  )
  $missing = @($required | Where-Object { -not (Test-Path $_) })
  if ($missing.Count -gt 0) {
    $fetchHint = if ($EffectiveToolchain -eq "MinGW") {
      "  powershell -File scripts\fetch_windows_ocr.ps1 -Toolchain MinGW"
    } else {
      "  powershell -File scripts\fetch_windows_ocr.ps1 -SourceDir 'C:\Program Files\Tesseract-OCR'"
    }
    Write-Host @"

OCR bundle incomplete. Run:
$fetchHint

Missing:
"@
    $missing | ForEach-Object { Write-Host "  $_" }
    exit 1
  }

  $dlls = @(Get-ChildItem $ocrRoot -Filter *.dll -File -ErrorAction SilentlyContinue)
  if ($dlls.Count -eq 0) {
    Write-Warning "No OCR DLLs in third_party\tesseract\windows — OCR will be disabled in the build."
    return
  }

  if ($EffectiveToolchain -eq "MinGW") {
    $libDir = Join-Path $ocrRoot "lib"
    $importLib = Get-ChildItem $libDir -Filter "libtesseract.dll.a" -File -ErrorAction SilentlyContinue |
      Select-Object -First 1
    if (-not $importLib) {
      Write-Warning @"
MinGW build requires libtesseract.dll.a in third_party\tesseract\windows\lib.
UB Mannheim .lib files are MSVC-only. Run:
  powershell -File scripts\fetch_windows_ocr.ps1 -Toolchain MinGW
"@
    }
  }
}

function Copy-InstallerRuntimeData {
  param([string]$DestRoot)

  $bundled = $false

  $db = Join-Path $RepoRoot "database\vlms.db"
  if (Test-Path $db) {
    $destDbDir = Join-Path $DestRoot "database"
    New-Item -ItemType Directory -Force -Path $destDbDir | Out-Null
    Copy-Item $db (Join-Path $destDbDir "vlms.db") -Force
    $sizeMb = [math]::Round((Get-Item $db).Length / 1MB, 1)
    Write-Host "  database/vlms.db ($sizeMb MB)"
    $bundled = $true
  }

  foreach ($subdir in @("books", "members")) {
    $src = Join-Path $RepoRoot "resources\$subdir"
    if (-not (Test-Path $src)) { continue }

    $entries = @(Get-ChildItem $src -Force -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -ne ".gitkeep" })
    if ($entries.Count -eq 0) { continue }

    $dest = Join-Path $DestRoot "resources\$subdir"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Copy-Item (Join-Path $src "*") $dest -Recurse -Force
    $count = (Get-ChildItem $dest -Recurse -File).Count
    Write-Host "  resources/$subdir/ ($count files)"
    $bundled = $true
  }

  if (-not $bundled) {
    Write-Warning @"
No pre-populated database or resources found.
The installer will create an empty database on first run.
For a full catalog build, run scripts/import_catalog.py locally or
scripts/fetch_installer_data.ps1 (downloads VLMS_installer_data.zip from releases).
"@
  }
}

function Test-MinGwOcrBundle {
  param([string]$OcrRoot)

  $importLib = Get-ChildItem (Join-Path $OcrRoot "lib") -Filter "libtesseract*.dll.a" -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if (-not $importLib) {
    throw @"
MinGW build requires MinGW OCR import libraries (libtesseract*.dll.a).
Found tessdata or MSVC .lib files only — remove third_party\tesseract\windows and run:
  scripts\fetch_windows_ocr.ps1 -Toolchain MinGW
"@
  }

  $dll = Get-ChildItem $OcrRoot -Filter "libtesseract-*.dll" -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if (-not $dll) {
    throw "MinGW OCR DLL (libtesseract-*.dll) missing under $OcrRoot"
  }
}

function Remove-MsvcRuntimeFiles {
  param([string]$StageRoot)

  $patterns = @(
    "VCRUNTIME*.dll",
    "MSVCP*.dll",
    "concrt140.dll",
    "vccorlib140.dll"
  )
  foreach ($pattern in $patterns) {
    Get-ChildItem $StageRoot -Filter $pattern -File -ErrorAction SilentlyContinue |
      Remove-Item -Force
  }
}

function Get-MinGwDllSearchDirs {
  param([string]$OcrRoot)

  $dirs = @(
    $OcrRoot,
    (Join-Path $OcrRoot "runtime")
  )

  if ($env:MSYS2_LOCATION) {
    $dirs += (Join-Path $env:MSYS2_LOCATION "mingw64\bin")
  }

  $dirs += "C:\msys64\mingw64\bin"
  return @($dirs | Select-Object -Unique)
}

function Copy-MinGwOcrRuntimeDlls {
  param(
    [string]$StageRoot,
    [string]$OcrRoot
  )

  $searchDirs = Get-MinGwDllSearchDirs -OcrRoot $OcrRoot

  Copy-MissingPeImports -StageDir $StageRoot -SearchDirs $searchDirs

  if (-not (Test-PeImportsSatisfied -StageDir $StageRoot)) {
    throw @"
Staged MinGW OCR dependencies are incomplete (e.g. libb2-1.dll, libbz2-1.dll, libexpat-1.dll, libiconv-2.dll).

Fix:
  1. Remove-Item -Recurse -Force third_party\tesseract\windows
  2. scripts\fetch_windows_ocr.ps1 -Toolchain MinGW
  3. Re-run scripts\build_windows_installer.ps1 -Toolchain MinGW
"@
  }
}

function Get-ProjectVersion {
  # Written by the top-level CMakeLists.txt at configure time from the latest
  # vX.Y.Z tag (cmake/GitVersion.cmake). Throwing beats guessing: a wrong
  # version here makes the installer's update level a number nobody chose.
  $versionFile = Join-Path $BuildDir "vlms_version.txt"
  if (-not (Test-Path $versionFile)) {
    throw "No $versionFile; configure the build first (it is written by CMakeLists.txt)."
  }
  $version = (Get-Content $versionFile -Raw).Trim()
  if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Unexpected version '$version' in $versionFile."
  }
  return $version
}

function New-ReleaseHistoryFiles {
  # The installer's "What's new" box and the CHANGELOG.md installed beside
  # vlms.exe, both rendered from the annotated release tags.
  param([string]$WhatsNewPath, [string]$ChangelogPath)
  $python = Get-Command python -ErrorAction SilentlyContinue
  if (-not $python) { $python = Get-Command python3 -ErrorAction SilentlyContinue }
  if (-not $python) { throw "Python is needed to render the release history (scripts/release/release.py)." }
  $tool = Join-Path $RepoRoot "scripts\release\release.py"
  & $python.Source $tool --repo $RepoRoot whats-new --out $WhatsNewPath
  if ($LASTEXITCODE -ne 0) { throw "release.py whats-new failed." }
  & $python.Source $tool --repo $RepoRoot changelog --out $ChangelogPath
  if ($LASTEXITCODE -ne 0) { throw "release.py changelog failed." }
  foreach ($line in Get-Content $WhatsNewPath -Encoding UTF8) {
    if ($line -notmatch '^(V\|[^|]+\|[^|]+\|[^|]+|W\|.*|-\|.*)$') {
      throw "Unexpected what's-new line: '$line'"
    }
  }
}

function Get-SchemaVersion {
  # The `PRAGMA user_version = N` that database/schema.sql ends with, and the
  # number the installer shows when it explains why an older database is not
  # kept by default. Read from the file so it cannot drift from what ships.
  $schema = Join-Path $RepoRoot "database\schema.sql"
  $content = Get-Content $schema -Raw
  $matchesFound = [regex]::Matches($content, '(?im)^\s*PRAGMA\s+user_version\s*=\s*(\d+)')
  if ($matchesFound.Count -eq 0) {
    throw "No 'PRAGMA user_version = N' found in $schema."
  }
  return $matchesFound[$matchesFound.Count - 1].Groups[1].Value
}

Write-Host "=== VLMS Windows installer build ==="
Write-Host "Repo:      $RepoRoot"
Write-Host "Build:     $BuildDir"
Write-Host "Stage:     $StageDir"
Write-Host "Toolchain: $Toolchain"

$QtRoot = Resolve-QtRoot -Hint $QtDir
if ($Toolchain -eq "Auto") {
  $Toolchain = Get-QtToolchain -QtRoot $QtRoot
  if ($Toolchain -eq "Unknown") {
    throw "Could not detect toolchain from Qt path '$QtRoot'. Pass -Toolchain MinGW or -Toolchain Msvc."
  }
}

$detected = Get-QtToolchain -QtRoot $QtRoot
if ($detected -ne "Unknown" -and $detected -ne $Toolchain) {
  throw "Qt kit '$QtRoot' is $detected but -Toolchain $Toolchain was requested. Use a matching Qt kit."
}

Write-Host "Qt:        $QtRoot ($Toolchain)"

if ($RequireInstallerData) {
  Write-Host "`n--- Fetch installer runtime data ---"
  & (Join-Path $RepoRoot "scripts\fetch_installer_data.ps1") -Require
}

Test-OcrBundle -EffectiveToolchain $Toolchain

if ($Toolchain -eq "MinGW") {
  $ocrRoot = Join-Path $RepoRoot "third_party\tesseract\windows"
  if (Test-Path (Join-Path $ocrRoot "tessdata\eng.traineddata")) {
    Test-MinGwOcrBundle -OcrRoot $ocrRoot
  }
}

if (-not $SkipBuild) {
  if ($Toolchain -eq "MinGW" -and (Test-Path $BuildDir)) {
    $cacheFile = Join-Path $BuildDir "CMakeCache.txt"
    if (Test-Path $cacheFile) {
      $cache = Get-Content $cacheFile -Raw
      if ($cache -match 'CMAKE_CXX_COMPILER:.*?=.*cl\.exe' -or $cache -match 'CMAKE_CXX_COMPILER:.*?=.*MSVC') {
        Write-Host "Removing stale MSVC build directory: $BuildDir"
        Remove-Item $BuildDir -Recurse -Force
      }
    }
  }

  New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

  $devPaths = if ($Configuration -eq "Release") { "OFF" } else { "ON" }
  Write-Host "`n--- Configure ($Configuration, VLMS_DEV_PATHS=$devPaths, $Toolchain) ---"

  $prefix = $QtRoot
  if ($env:MSYS2_LOCATION) {
    $mingw64 = Join-Path $env:MSYS2_LOCATION "mingw64"
    if (Test-Path $mingw64) { $prefix = "$mingw64;$QtRoot" }
  }

  $cmakeArgs = @(
    "-S", $RepoRoot,
    "-B", $BuildDir,
    "-DCMAKE_BUILD_TYPE=$Configuration",
    # The installer ships the application only; the tests run in the Linux CI
    # jobs. Building them here doubled the compile and the memory it needs.
    "-DVLMS_BUILD_TESTS=OFF",
    "-DVLMS_DEV_PATHS=$devPaths",
    "-DVLMS_INSTALLER_STAGE_DIR=$StageDir",
    "-DCMAKE_PREFIX_PATH=$prefix"
  )

  if ($Toolchain -eq "MinGW") {
    $mingwBin = Resolve-MinGwBinDir -QtRoot $QtRoot
    $cmakeArgs += @(
      "-DCMAKE_C_COMPILER=$(Join-Path $mingwBin 'gcc.exe')",
      "-DCMAKE_CXX_COMPILER=$(Join-Path $mingwBin 'g++.exe')"
    )
    Write-Host "MinGW:     $mingwBin"
  }

  if ($env:CMAKE_GENERATOR) {
    $cmakeArgs += @("-G", $env:CMAKE_GENERATOR)
  } elseif ($Toolchain -eq "MinGW") {
    $cmakeArgs += @("-G", "MinGW Makefiles")
  }

  cmake @cmakeArgs

  Write-Host "`n--- Build ---"
  # A bare --parallel is an unbounded "make -j" under MinGW Makefiles: every
  # translation unit at once, and the runner ran out of memory (cc1plus: out of
  # memory allocating 65536 bytes). One job per processor.
  $jobs = [Math]::Max(1, [int]$env:NUMBER_OF_PROCESSORS)
  cmake --build $BuildDir --config $Configuration --parallel $jobs
}

Write-Host "`n--- Stage install tree ---"
if (Test-Path $StageDir) {
  Remove-Item $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

cmake --install $BuildDir --prefix $StageDir --config $Configuration

Write-Host "`n--- Bundle runtime data (database + resources) ---"
Copy-InstallerRuntimeData -DestRoot $StageDir

$ExePath = Join-Path $StageDir "vlms.exe"
if (-not (Test-Path $ExePath)) {
  throw "Staged executable not found: $ExePath"
}

Write-Host "`n--- Deploy Qt runtime ---"
$QtBin = Join-Path $QtRoot "bin"
$WindeployQt = Join-Path $QtBin "windeployqt.exe"
$deployArgs = @(
  "--dir", $StageDir,
  "--$($Configuration.ToLower())",
  "--no-translations",
  "--no-opengl-sw",
  $ExePath
)
if ($Toolchain -eq "Msvc") {
  $deployArgs += "--compiler-runtime"
  Write-Host "Including MSVC runtime DLLs via windeployqt --compiler-runtime"
} else {
  Write-Host "MinGW build: no MSVC runtime; vlms.exe uses static libgcc/libstdc++/winpthread"
}

& $WindeployQt @deployArgs

if ($Toolchain -eq "MinGW") {
  Remove-MsvcRuntimeFiles -StageRoot $StageDir

  $ocrRoot = Join-Path $RepoRoot "third_party\tesseract\windows"
  $mingwSearchDirs = Get-MinGwDllSearchDirs -OcrRoot $ocrRoot
  Write-Host "`n--- Bundle MinGW runtime DLLs (sqlite3 and other PE imports) ---"
  Copy-MissingPeImports -StageDir $StageDir -SearchDirs $mingwSearchDirs

  $ocrDlls = @(Get-ChildItem $ocrRoot -Filter *.dll -File -ErrorAction SilentlyContinue)
  if ($ocrDlls.Count -gt 0) {
    Write-Host "`n--- Bundle MinGW OCR runtime DLLs (for bundled tesseract/leptonica) ---"
    Copy-MinGwOcrRuntimeDlls -StageRoot $StageDir -OcrRoot $ocrRoot
  } elseif (-not (Test-PeImportsSatisfied -StageDir $StageDir)) {
    throw "Staged MinGW dependencies are incomplete (expected libsqlite3-0.dll from MSYS2 mingw-w64-x86_64-sqlite3)."
  }

  Write-Host "`n--- Verify no MSVC runtime dependencies ---"
  & (Join-Path $RepoRoot "scripts\verify_no_msvc_runtime.ps1") -StageDir $StageDir
}

$Schema = Join-Path $StageDir "schema.sql"
if (-not (Test-Path $Schema)) {
  throw "schema.sql missing from staged folder: $Schema"
}

$TessDir = Join-Path $StageDir "tessdata"
if (-not (Test-Path $TessDir)) {
  Write-Warning "tessdata/ not staged — OCR disabled in installer. Populate third_party/tesseract/windows."
}

Write-Host "`nStaged application folder:"
Get-ChildItem $StageDir | Select-Object -First 20 | Format-Table Name, Length -AutoSize
Write-Host "  ($((Get-ChildItem $StageDir -Recurse -File).Count) files total)"

Write-Host "`n--- Verify staged installer layout ---"
if ($RequireInstallerData) {
  & (Join-Path $RepoRoot "scripts\verify_installer_stage.ps1") -StageDir $StageDir -RequireInstallerData
} else {
  & (Join-Path $RepoRoot "scripts\verify_installer_stage.ps1") -StageDir $StageDir
}

if ($SkipInstaller) {
  Write-Host "`nSkipInstaller set — staged folder ready at:"
  Write-Host "  $StageDir"
  exit 0
}

$Version = Get-ProjectVersion
$SchemaVersion = Get-SchemaVersion
Write-Host "Version:   $Version"
Write-Host "Schema:    user_version $SchemaVersion"

$WhatsNewFile = Join-Path $BuildDir "whats_new.txt"
New-ReleaseHistoryFiles -WhatsNewPath $WhatsNewFile -ChangelogPath (Join-Path $StageDir "CHANGELOG.md")

Write-Host "`n--- Inno Setup ---"
$Iscc = Resolve-Iscc -Hint $InnoSetupDir
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

$stageForIss = (Resolve-Path $StageDir).Path
& $Iscc `
  "/DStageDir=$stageForIss" `
  "/DMyAppVersion=$Version" `
  "/DSchemaVersion=$SchemaVersion" `
  "/DWhatsNewFile=$((Resolve-Path $WhatsNewFile).Path)" `
  $IssFile
if ($LASTEXITCODE -ne 0) {
  throw "Inno Setup failed with exit code $LASTEXITCODE."
}

$Installer = Get-ChildItem $DistDir -Filter "VLMS_Setup_$Version.exe" -ErrorAction SilentlyContinue |
  Select-Object -First 1
if (-not $Installer) {
  $Installer = Get-ChildItem $DistDir -Filter "VLMS_Setup_*.exe" |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
}

if ($Installer) {
  Write-Host "`n=== Done ==="
  Write-Host "Installer: $($Installer.FullName)"
  Write-Host "Size:      $([math]::Round($Installer.Length / 1MB, 1)) MB"
  if ($Toolchain -eq "MinGW") {
    Write-Host "Runtime:   self-contained (Qt + MinGW OCR DLLs; no Visual C++ Redistributable)"
  }
} else {
  throw "Installer .exe not found under $DistDir"
}
