#Requires -Version 5.1
<#
.SYNOPSIS
  Fail the build when staged binaries import the MSVC C++ runtime (VCRUNTIME140 / MSVCP140).
#>

param(
  [Parameter(Mandatory = $true)]
  [string]$StageDir
)

$ErrorActionPreference = "Stop"

$MsvcImportPatterns = @(
  "VCRUNTIME140.dll",
  "VCRUNTIME140_1.dll",
  "MSVCP140.dll",
  "MSVCP140_1.dll",
  "MSVCP140_2.dll",
  "MSVCR120.dll",
  "MSVCR110.dll"
)

function Resolve-Objdump {
  $candidates = @(
    $env:OBJDUMP,
    (Get-Command objdump.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
  ) | Where-Object { $_ -and (Test-Path $_) }

  foreach ($path in $candidates) {
    return $path
  }

  $msysObjdump = "C:\msys64\mingw64\bin\objdump.exe"
  if (Test-Path $msysObjdump) { return $msysObjdump }

  throw "objdump.exe not found. Install MSYS2 mingw-w64-binutils or add objdump to PATH."
}

function Get-PeImports {
  param(
    [string]$Objdump,
    [string]$BinaryPath
  )

  $imports = @()
  $output = & $Objdump -p $BinaryPath 2>$null
  if (-not $output) { return $imports }

  foreach ($line in $output) {
    if ($line -match '^\s*DLL Name:\s*(.+)\s*$') {
      $imports += $Matches[1]
    }
  }
  return $imports
}

$objdump = Resolve-Objdump
$binaries = @(
  Get-ChildItem $StageDir -Filter *.dll -File -ErrorAction SilentlyContinue
  Get-ChildItem $StageDir -Filter *.exe -File -ErrorAction SilentlyContinue
)

$failures = @()
foreach ($binary in $binaries) {
  $imports = Get-PeImports -Objdump $objdump -BinaryPath $binary.FullName
  $bad = @($imports | Where-Object {
    $name = $_.ToUpperInvariant()
    foreach ($pattern in $MsvcImportPatterns) {
      if ($name -eq $pattern.ToUpperInvariant()) { return $true }
    }
    return $false
  })

  if ($bad.Count -gt 0) {
    $failures += [PSCustomObject]@{
      File = $binary.Name
      Imports = ($bad -join ", ")
    }
  }
}

$leftover = @(
  "VCRUNTIME140.dll",
  "VCRUNTIME140_1.dll",
  "MSVCP140.dll"
) | ForEach-Object {
  $path = Join-Path $StageDir $_
  if (Test-Path $path) { $_ }
}

if ($leftover.Count -gt 0) {
  throw @"
MSVC runtime DLL files must not be shipped with a MinGW build.
Remove from stage: $($leftover -join ', ')

Rebuild with -Toolchain MinGW, Qt mingw_64, and:
  scripts\fetch_windows_ocr.ps1 -Toolchain MinGW
"@
}

if ($failures.Count -gt 0) {
  Write-Host "MSVC runtime imports detected (MinGW build required):"
  foreach ($item in $failures) {
    Write-Host "  $($item.File) -> $($item.Imports)"
  }
  throw @"
This installer would require Visual C++ Redistributable on customer PCs.

Usual causes:
  - Qt msvc2022_64 kit used instead of mingw_64
  - third_party/tesseract/windows contains UB Mannheim / vcpkg (MSVC) DLLs
  - stale build/ folder from an older MSVC configure

Fix:
  1. Remove-Item -Recurse -Force build, third_party\tesseract\windows
  2. scripts\fetch_windows_ocr.ps1 -Toolchain MinGW
  3. scripts\build_windows_installer.ps1 -Toolchain MinGW -QtDir '...\mingw_64'
"@
}

Write-Host "No MSVC runtime imports in staged exe/dll files."
