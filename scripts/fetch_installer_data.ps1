#Requires -Version 5.1
<#
.SYNOPSIS
  Ensure the sample cover images and member photographs exist before building
  the installer. They are not tracked; database/vlms.db is, and the zip does not
  carry it.
#>

param(
  [switch]$Require,
  # The release that holds the catalogue data zip. Not "latest": releases cut
  # automatically carry only the installer. Keep in step with
  # DATA_TAG in fetch_runtime_data.sh.
  [string]$DataTag = "installer-data-2"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$BooksDir = Join-Path $RepoRoot "resources\books"
$ZipName = "VLMS_installer_data.zip"
# Names the data release last unpacked here, so a bump of DataTag fetches again.
$MarkerPath = Join-Path $RepoRoot "resources\.installer-data"

function Test-InstallerDataPresent {
  $bookFiles = @(Get-ChildItem $BooksDir -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne ".gitkeep" })
  return $bookFiles.Count -gt 0
}

function Test-InstallerDataCurrent {
  if (-not (Test-InstallerDataPresent)) { return $false }
  if ($env:VLMS_INSTALLER_DATA_URL) { return $true }
  if (-not (Test-Path $MarkerPath)) { return $false }
  return (Get-Content $MarkerPath -Raw).Trim() -eq $DataTag
}

function Expand-InstallerDataZip {
  param([string]$ZipPath)

  $temp = Join-Path $env:TEMP ("vlms_installer_data_" + [guid]::NewGuid().ToString("N"))
  New-Item -ItemType Directory -Force -Path $temp | Out-Null
  try {
    Expand-Archive -Path $ZipPath -DestinationPath $temp -Force

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

if (Test-InstallerDataCurrent) {
  Write-Host "Installer runtime data already present."
  exit 0
}

Write-Host "Cover images not found — downloading release asset..."
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
  if (-not $env:VLMS_INSTALLER_DATA_URL) { Set-Content -Path $MarkerPath -Value $DataTag }
  Write-Host "Installer runtime data ready."
} else {
  Fail-IfRequired "Download completed but cover images are still missing."
}
