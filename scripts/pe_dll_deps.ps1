# Shared PE dependency helpers for MinGW Windows packaging.
# Dot-source from build/fetch/verify scripts:
#   . (Join-Path $PSScriptRoot "pe_dll_deps.ps1")

$Script:PeSystemDllPattern = '^(?i)(advapi32|bcrypt|comctl32|comdlg32|crypt32|dnsapi|dwmapi|gdi32|gdiplus|imm32|iphlpapi|kernel32|kernelbase|msimg32|msvcrt|msvcp_win|mswsock|netapi32|normaliz|ntdll|ole32|oleaut32|psapi|rpcrt4|secur32|setupapi|shell32|shlwapi|ucrtbase|user32|uxtheme|version|winmm|winspool|wldap32|ws2_32|wsock32)\.dll$'

function Test-PeSystemDll {
  param([Parameter(Mandatory = $true)][string]$Name)

  if ($Name -match $Script:PeSystemDllPattern) { return $true }

  # API set forwarders (virtual DLL names resolved by the Windows loader).
  if ($Name -match '^(?i)(api-ms-|ext-ms-win-)') { return $true }

  $windir = if ($env:WINDIR) { $env:WINDIR } else { $env:SystemRoot }
  if (-not $windir) { return $false }

  foreach ($dir in @(
    (Join-Path $windir "System32"),
    (Join-Path $windir "SysWOW64")
  )) {
    if (Test-Path (Join-Path $dir $Name)) {
      return $true
    }
  }

  return $false
}

function Resolve-PeObjdump {
  $candidates = @(
    $env:OBJDUMP,
    (Get-Command objdump.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)
  ) | Where-Object { $_ -and (Test-Path $_) }

  foreach ($path in $candidates) {
    return $path
  }

  $msysObjdump = "C:\msys64\mingw64\bin\objdump.exe"
  if (Test-Path $msysObjdump) { return $msysObjdump }

  return $null
}

function Get-PeImportNames {
  param(
    [Parameter(Mandatory = $true)]
    [string]$BinaryPath,
    [string]$Objdump = ""
  )

  if (-not $Objdump) {
    $Objdump = Resolve-PeObjdump
  }
  if (-not $Objdump) { return @() }

  $imports = @()
  $output = & $Objdump -p $BinaryPath 2>$null
  if (-not $output) { return @() }

  foreach ($line in $output) {
    if ($line -match '^\s*DLL Name:\s*(.+)\s*$') {
      $imports += $Matches[1].Trim()
    }
  }
  return $imports
}

function Resolve-PeDllClosure {
  param(
    [Parameter(Mandatory = $true)]
    [string[]]$SeedPaths,
    [Parameter(Mandatory = $true)]
    [string]$SearchDir,
    [string]$Objdump = ""
  )

  if (-not $Objdump) {
    $Objdump = Resolve-PeObjdump
  }

  $closure = @{}
  $queue = [System.Collections.Queue]::new()

  foreach ($seed in $SeedPaths) {
    if (-not (Test-Path $seed)) { continue }
    $leaf = Split-Path -Leaf $seed
    if (-not $closure.ContainsKey($leaf)) {
      $closure[$leaf] = (Resolve-Path $seed).Path
      $queue.Enqueue($closure[$leaf])
    }
  }

  while ($queue.Count -gt 0) {
    $current = $queue.Dequeue()
    foreach ($importName in (Get-PeImportNames -BinaryPath $current -Objdump $Objdump)) {
      if ($closure.ContainsKey($importName)) { continue }
      if (Test-PeSystemDll -Name $importName) { continue }

      $candidate = Join-Path $SearchDir $importName
      if (-not (Test-Path $candidate)) { continue }

      $resolved = (Resolve-Path $candidate).Path
      $closure[$importName] = $resolved
      $queue.Enqueue($resolved)
    }
  }

  return $closure
}

function Copy-MissingPeImports {
  param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir,
    [Parameter(Mandatory = $true)]
    [string[]]$SearchDirs
  )

  $objdump = Resolve-PeObjdump
  if (-not $objdump) {
    if ($env:CI -or $env:GITHUB_ACTIONS) {
      throw "objdump.exe not found; cannot resolve PE dependencies in CI."
    }
    Write-Warning "objdump.exe not found; skipping PE dependency closure copy."
    return
  }

  $binaries = @(
    Get-ChildItem $StageDir -Filter *.dll -File -ErrorAction SilentlyContinue
    Get-ChildItem $StageDir -Filter *.exe -File -ErrorAction SilentlyContinue
  )

  $copiedAny = $true
  while ($copiedAny) {
    $copiedAny = $false
    $binaries = @(
      Get-ChildItem $StageDir -Filter *.dll -File -ErrorAction SilentlyContinue
      Get-ChildItem $StageDir -Filter *.exe -File -ErrorAction SilentlyContinue
    )

    foreach ($binary in $binaries) {
      foreach ($importName in (Get-PeImportNames -BinaryPath $binary.FullName -Objdump $objdump)) {
        if (Test-PeSystemDll -Name $importName) { continue }

        $dest = Join-Path $StageDir $importName
        if (Test-Path $dest) { continue }

        foreach ($searchDir in $SearchDirs) {
          if (-not (Test-Path $searchDir)) { continue }
          $src = Join-Path $searchDir $importName
          if (-not (Test-Path $src)) { continue }

          Copy-Item $src $dest -Force
          Write-Host "  Bundled dependency: $importName (required by $($binary.Name))"
          $copiedAny = $true
          break
        }
      }
    }
  }
}

function Test-PeImportsSatisfied {
  param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir
  )

  $objdump = Resolve-PeObjdump
  if (-not $objdump) {
    if ($env:CI -or $env:GITHUB_ACTIONS) {
      throw "objdump.exe not found; cannot verify PE dependencies in CI."
    }
    Write-Warning "objdump.exe not found; skipping PE dependency verification."
    return $true
  }

  $missing = @{}
  $binaries = @(
    Get-ChildItem $StageDir -Filter *.dll -File -ErrorAction SilentlyContinue
    Get-ChildItem $StageDir -Filter *.exe -File -ErrorAction SilentlyContinue
  )

  foreach ($binary in $binaries) {
    foreach ($importName in (Get-PeImportNames -BinaryPath $binary.FullName -Objdump $objdump)) {
      if (Test-PeSystemDll -Name $importName) { continue }
      if (Test-Path (Join-Path $StageDir $importName)) { continue }

      if (-not $missing.ContainsKey($importName)) {
        $missing[$importName] = @()
      }
      $missing[$importName] += $binary.Name
    }
  }

  if ($missing.Count -eq 0) {
    return $true
  }

  Write-Host "Unresolved PE imports in staged folder:"
  foreach ($entry in ($missing.GetEnumerator() | Sort-Object Name)) {
    Write-Host "  $($entry.Key) <- $($entry.Value -join ', ')"
  }
  return $false
}
