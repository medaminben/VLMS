#Requires -Version 5.1
<#
.SYNOPSIS
  Ensure pre-populated database and resources exist before building the installer.
#>

param(
  [switch]$Require,
  # The release that holds the catalogue data zip. Not "latest": releases cut
  # automatically carry only the installer and CHANGELOG.md.
  [string]$DataTag = "v0.1.0"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$DbPath = Join-Path $RepoRoot "database\vlms.db"
$BooksDir = Join-Path $RepoRoot "resources\books"
$ZipName = "VLMS_installer_data.zip"

function Test-InstallerDataPresent {
  if (-not (Test-Path $DbPath)) { return $false }
  $bookFiles = @(Get-ChildItem $BooksDir -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne ".gitkeep" })
  return $bookFiles.Count -gt 0
}

function Expand-InstallerDataZip {
  param([string]$ZipPath)

  $temp = Join-Path $env:TEMP ("vlms_installer_data_" + [guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Force -Path $temp | Out-Null
  try {
    Expand-Archive -Path $ZipPath -DestinationPath $temp -Force

    $db = Get-ChildItem $temp -Recurse -Filter "vlms.db" -File -ErrorAction SilentlyContinue |
      Select-Object -First 1
    if ($db) {
      New-Item -ItemType Directory -Force -Path (Split-Path $DbPath) | Out-Null
      Copy-Item $db.FullName $DbPath -Force
      Write-Host "  Installed database/vlms.db"
    }

    foreach ($subdir in @("books", "members")) {
      $src = Get-ChildItem $temp -Recurse -Directory -Filter $subdir -ErrorAction SilentlyContinue |
        Where-Object { $_.Parent.Name -eq "resources" } |
        Select-Object -First 1
      if ($src) {
        $dest = Join-Path $RepoRoot "resources\$subdir"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item (Join-Path $src.FullName "*") $dest -Recurse -Force
        $count = (Get-ChildItem $dest -Recurse -File | Where-Object { $_.Name -ne ".gitkeep" }).Count
        Write-Host "  Installed resources/$subdir/ ($count files)"
      }
    }
  } finally {
    Remove-Item $temp -Recurse -Force -ErrorAction SilentlyContinue
  }
}

function Download-ReleaseAsset {
  param(
    [string]$RepoSlug,
    [string]$AssetName,
    [string]$OutFile
  )

  $headers = @{}
  if ($env:GITHUB_TOKEN) {
    $headers.Authorization = "Bearer $env:GITHUB_TOKEN"
  }

  try {
    $release = Invoke-RestMethod `
      -Uri "https://api.github.com/repos/$RepoSlug/releases/tags/$DataTag" `
      -Headers $headers
  } catch {
    Write-Host "No release $DataTag available for installer data."
    return $false
  }

  $asset = $release.assets | Where-Object { $_.name -eq $AssetName } | Select-Object -First 1
  if (-not $asset) { return $false }

  Write-Host "Downloading $AssetName from release $($release.tag_name)..."
  if (-not $env:GITHUB_TOKEN) {
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $OutFile
  } else {
    Invoke-WebRequest `
      -Uri $asset.url `
      -Headers @{ Authorization = "Bearer $env:GITHUB_TOKEN"; Accept = "application/octet-stream" } `
      -OutFile $OutFile
  }
  return $true
}

function Fail-IfRequired {
  param([string]$Message)

  if ($Require) {
    throw $Message
  }
  Write-Warning $Message
}

if (Test-InstallerDataPresent) {
  Write-Host "Installer runtime data already present."
  exit 0
}

Write-Host "Pre-populated database/resources not found — downloading release asset..."
$ZipPath = Join-Path $RepoRoot $ZipName

if ($env:VLMS_INSTALLER_DATA_URL) {
  Write-Host "Downloading from VLMS_INSTALLER_DATA_URL"
  Invoke-WebRequest -Uri $env:VLMS_INSTALLER_DATA_URL -OutFile $ZipPath
} else {
  $remote = git -C $RepoRoot remote get-url origin 2>$null
  if (-not $remote -or $remote -notmatch 'github\.com[:/](?<owner>[^/]+)/(?<repo>[^/.]+)') {
    Fail-IfRequired "Could not determine GitHub remote; installer catalog data is missing."
    exit 0
  }
  $repoSlug = "$($Matches.owner)/$($Matches.repo -replace '\.git$', '')"
  if (-not (Download-ReleaseAsset -RepoSlug $repoSlug -AssetName $ZipName -OutFile $ZipPath)) {
    Fail-IfRequired "No $ZipName on release $DataTag; installer catalog data is missing."
    exit 0
  }
}

Expand-InstallerDataZip -ZipPath $ZipPath
Remove-Item $ZipPath -Force -ErrorAction SilentlyContinue

if (Test-InstallerDataPresent) {
  Write-Host "Installer runtime data ready."
} else {
  Fail-IfRequired "Download completed but database/covers still missing."
}
