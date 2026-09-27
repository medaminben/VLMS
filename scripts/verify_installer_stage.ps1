#Requires -Version 5.1
<#
.SYNOPSIS
  Verify the staged Windows installer folder contains required files and optional catalog data.
#>

param(
  [Parameter(Mandatory = $true)]
  [string]$StageDir,
  [switch]$RequireInstallerData
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "pe_dll_deps.ps1")

function Test-ResourceTreeHasFiles {
  param([string]$Root)

  if (-not (Test-Path $Root)) { return $false }
  $files = @(Get-ChildItem $Root -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne ".gitkeep" })
  return $files.Count -gt 0
}

function Get-SqliteUserVersion {
  # PRAGMA user_version lives in the SQLite file header: a big-endian 32-bit
  # integer at offset 60. Reading it directly avoids needing sqlite3.exe on the
  # build machine. Returns $null when the file is not a SQLite database.
  param([string]$Path)

  $stream = [System.IO.File]::OpenRead($Path)
  try {
    $header = New-Object byte[] 64
    if ($stream.Read($header, 0, 64) -lt 64) { return $null }
    $magic = [System.Text.Encoding]::ASCII.GetString($header, 0, 15)
    if ($magic -ne "SQLite format 3") { return $null }
    return ([int]$header[60] -shl 24) -bor ([int]$header[61] -shl 16) -bor
           ([int]$header[62] -shl 8) -bor [int]$header[63]
  } finally {
    $stream.Dispose()
  }
}

function Get-SchemaFileVersion {
  param([string]$Path)

  $found = [regex]::Matches((Get-Content $Path -Raw), '(?im)^\s*PRAGMA\s+user_version\s*=\s*(\d+)')
  if ($found.Count -eq 0) { return $null }
  return [int]$found[$found.Count - 1].Groups[1].Value
}

$required = @(
  (Join-Path $StageDir "vlms.exe"),
  (Join-Path $StageDir "schema.sql"),
  (Join-Path $StageDir "platforms\qwindows.dll")
)

$missing = @($required | Where-Object { -not (Test-Path $_) })
if ($missing.Count -gt 0) {
  Write-Host "Staged installer missing required files:"
  $missing | ForEach-Object { Write-Host "  $_" }
  exit 1
}

$db = Join-Path $StageDir "database\vlms.db"
$books = Join-Path $StageDir "resources\books"
$members = Join-Path $StageDir "resources\members"

$hasDb = Test-Path $db
$hasBooks = Test-ResourceTreeHasFiles -Root $books
$hasMembers = Test-ResourceTreeHasFiles -Root $members

$sqliteDll = @(Get-ChildItem $StageDir -File |
  Where-Object { $_.Name -match '(?i)sqlite3' })
if ($sqliteDll.Count -eq 0) {
  throw "Staged installer is missing the sqlite3 runtime DLL (Core links the C API, not Qt Sql)."
}

Write-Host "Staged installer layout:"
Write-Host "  vlms.exe          OK"
Write-Host "  schema.sql             OK"
Write-Host "  platforms/qwindows.dll OK"
Write-Host "  $($sqliteDll[0].Name)  OK"
Write-Host "  database/vlms.db  $(if ($hasDb) { 'OK' } else { 'MISSING' })"
Write-Host "  resources/books/       $(if ($hasBooks) { 'OK' } else { 'MISSING' })"
Write-Host "  resources/members/     $(if ($hasMembers) { 'OK' } else { 'empty or missing' })"

if ($hasDb) {
  # The bundled catalogue must already be at the schema this build ships, or
  # every fresh install starts by migrating a database on first launch -- which
  # is exactly the state the installer's previous-version check exists to avoid.
  # This is how a stale data zip (0.1.0 shipped user_version 0) gets caught.
  $expected = Get-SchemaFileVersion -Path (Join-Path $StageDir "schema.sql")
  $actual = Get-SqliteUserVersion -Path $db

  if ($null -eq $actual) {
    throw "$db is not a SQLite database."
  }
  Write-Host "  database schema         user_version $actual (schema.sql expects $expected)"

  if ($null -ne $expected -and $actual -ne $expected) {
    $message = "Bundled database is at schema $actual but this build ships schema $expected. " +
      "Refresh database/vlms.db (open it once with this build, or re-run scripts/import_catalog.py) " +
      "and rebuild."
    if ($RequireInstallerData) { throw $message }
    Write-Warning $message
  }
}

if ($RequireInstallerData -and -not $hasDb) {
  throw "database/vlms.db is required but missing from $StageDir"
}
if ($RequireInstallerData -and -not $hasBooks) {
  throw "resources/books/ is required but missing or empty in $StageDir"
}

if (-not $hasDb -or -not $hasBooks) {
  Write-Warning "Installer will ship without pre-populated catalog data."
}

if (-not (Test-PeImportsSatisfied -StageDir $StageDir)) {
  throw "Staged runtime DLL dependencies are incomplete (sqlite3 and/or OCR). Rebuild the installer after installing MSYS2 mingw-w64-x86_64-sqlite3 and refreshing third_party/tesseract/windows."
}

Write-Host "Stage verification passed."
