#Requires -Version 5.1
<#
.SYNOPSIS
  Record where every DLL in the staged installer came from, and copy the
  licence files of the MSYS2 packages among them.

.DESCRIPTION
  The static notices (LICENSE, THIRD_PARTY_NOTICES.txt, licenses\*.txt) are
  installed by CMake. What this adds is the part only the finished stage can
  answer: the DLLs windeployqt and Copy-MissingPeImports pulled in. For each
  one it writes, to licenses\BUNDLED_FILES.txt, whether it is Qt, an MSYS2
  package (found with pacman, whose licence files are copied to
  licenses\msys2\<package>\), or the MinGW GCC runtime.

  A DLL it cannot place is listed as UNKNOWN and warned about: its licence
  has to be found before the installer is released.

.PARAMETER StageDir
  The staged application folder (build\installer\stage).
.PARAMETER QtRoot
  The Qt kit the build used, e.g. C:\Qt\6.8.1\mingw_64.
.PARAMETER MsysRoot
  The MSYS2 root. Defaults to $env:MSYS2_LOCATION, then C:\msys64.
#>

param(
  [Parameter(Mandatory = $true)]
  [string]$StageDir,
  [Parameter(Mandatory = $true)]
  [string]$QtRoot,
  [string]$MsysRoot = ""
)

$ErrorActionPreference = "Stop"

if (-not $MsysRoot) {
  if ($env:MSYS2_LOCATION) { $MsysRoot = $env:MSYS2_LOCATION } else { $MsysRoot = "C:\msys64" }
}

$MingwBin = Join-Path $MsysRoot "mingw64\bin"
$Pacman = Join-Path $MsysRoot "usr\bin\pacman.exe"
$HasPacman = Test-Path $Pacman
if (-not $HasPacman) {
  Write-Warning "pacman not found at $Pacman; MSYS2 DLLs are listed without their package licences."
}

$LicensesDir = Join-Path $StageDir "licenses"
$Msys2LicensesDir = Join-Path $LicensesDir "msys2"
New-Item -ItemType Directory -Force -Path $LicensesDir | Out-Null

# libgcc, libstdc++ and winpthreads. Qt's MinGW kit carries its own copies in
# its bin folder, so finding one there does not make it Qt.
$RuntimeDllPattern = '^(?i)(libgcc_s_[a-z0-9]+-1|libstdc\+\+-6|libwinpthread-1)\.dll$'

function Invoke-Pacman {
  param([string[]]$Arguments)
  # pacman writes "No package owns ..." to stderr; that is an answer, not a
  # failure, and Stop would turn it into one under Windows PowerShell.
  $ErrorActionPreference = "Continue"
  $output = & $Pacman @Arguments 2>$null
  if ($LASTEXITCODE -ne 0) { return @() }
  return @($output | Where-Object { $_ } | ForEach-Object { "$_".Trim() })
}

function Get-MsysOwner {
  param([string]$DllName)
  if (-not $HasPacman) { return $null }
  if (-not (Test-Path (Join-Path $MingwBin $DllName))) { return $null }
  # A POSIX path: pacman's database knows files as /mingw64/bin/...
  $owner = @(Invoke-Pacman @("-Qqo", "/mingw64/bin/$DllName"))
  if ($owner.Count -eq 0) { return $null }
  return $owner[0]
}

function Copy-PackageLicences {
  param([string]$Package)
  $copied = @()
  $marker = "/share/licenses/"
  foreach ($file in (Invoke-Pacman @("-Qql", $Package))) {
    $at = $file.IndexOf($marker)
    if ($at -lt 0 -or $file.EndsWith("/")) { continue }
    $source = Join-Path $MsysRoot ($file.TrimStart("/") -replace "/", "\")
    if (-not (Test-Path $source -PathType Leaf)) { continue }
    # Keep the path below share/licenses/, so two COPYING files in one
    # package do not overwrite each other.
    $relative = $file.Substring($at + $marker.Length) -replace "/", "\"
    $target = Join-Path (Join-Path $Msys2LicensesDir $Package) $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item $source $target -Force
    $copied += "licenses\msys2\$Package\$relative"
  }
  return $copied
}

$stageFull = (Resolve-Path $StageDir).Path.TrimEnd("\")
$rows = @()
$packages = @{}
$unknown = @()

foreach ($dll in (Get-ChildItem $stageFull -Recurse -File -Filter *.dll | Sort-Object FullName)) {
  $relative = $dll.FullName.Substring($stageFull.Length + 1)
  $name = $dll.Name
  $origin = $null

  if ($relative.Contains("\") -and (Test-Path (Join-Path (Join-Path $QtRoot "plugins") $relative))) {
    $origin = "Qt 6 plugin - licenses\LGPL-3.0.txt"
  } elseif ($name -match '^(?i)Qt6' -and (Test-Path (Join-Path (Join-Path $QtRoot "bin") $name))) {
    $origin = "Qt 6 - licenses\LGPL-3.0.txt"
  } else {
    $owner = Get-MsysOwner -DllName $name
    if ($owner) {
      $packages[$owner] = $true
      $origin = "MSYS2 package $owner - licenses\msys2\$owner\"
    } elseif ($name -match $RuntimeDllPattern) {
      $origin = "MinGW-w64 GCC runtime - see THIRD_PARTY_NOTICES.txt"
    } elseif (Test-Path (Join-Path (Join-Path $QtRoot "bin") $name)) {
      $origin = "Qt 6 kit - licenses\LGPL-3.0.txt"
    }
  }

  if (-not $origin) {
    $origin = "UNKNOWN - find its licence before releasing"
    $unknown += $relative
  }
  $rows += ("{0,-44} {1}" -f $relative, $origin)
}

# vlms.exe links libstdc++ and winpthreads statically, so their licences
# belong in the notices even when no DLL of theirs is staged.
foreach ($runtimeDll in @("libwinpthread-1.dll", "libstdc++-6.dll")) {
  $owner = Get-MsysOwner -DllName $runtimeDll
  if ($owner) { $packages[$owner] = $true }
}

$lines = @(
  "Files bundled with VLMS by the Windows installer build",
  "======================================================",
  "",
  "Written by scripts/write_bundled_licenses.ps1 when this installer was built.",
  "See THIRD_PARTY_NOTICES.txt for the components themselves.",
  "",
  "DLLs",
  "----"
)
$lines += $rows
$lines += @("", "MSYS2 packages and their licence files", "--------------------------------------")

foreach ($package in ($packages.Keys | Sort-Object)) {
  $version = @(Invoke-Pacman @("-Q", $package))
  if ($version.Count -gt 0) { $lines += $version[0] } else { $lines += $package }
  $copied = @(Copy-PackageLicences -Package $package)
  if ($copied.Count -gt 0) {
    $lines += ($copied | ForEach-Object { "    $_" })
  } else {
    $lines += "    (the package installs no licence file; see https://packages.msys2.org/package/$package)"
  }
}

$out = Join-Path $LicensesDir "BUNDLED_FILES.txt"
[System.IO.File]::WriteAllLines($out, [string[]]$lines, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "Recorded $($rows.Count) DLLs and $($packages.Count) MSYS2 packages in $out"
if ($unknown.Count -gt 0) {
  Write-Warning ("No known source for these DLLs; find their licences before releasing:`n  " +
    ($unknown -join "`n  "))
}
