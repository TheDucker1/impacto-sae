param([string]$Usrdir, [string]$SaveDirectory)
$ErrorActionPreference = 'Stop'
try {
  if (!$Usrdir) {
    $locationFile = Join-Path $PSScriptRoot 'game-data.path'
    if (Test-Path -LiteralPath $locationFile) {
      $Usrdir = (Get-Content -LiteralPath $locationFile -Raw).Trim()
    }
  }
  if (!$Usrdir) {
    throw 'Set game-data.path to your original USRDIR folder, or supply -Usrdir PATH.'
  }
  $data = (Resolve-Path -LiteralPath $Usrdir).Path
  foreach ($required in @('resource.dat', 'first.dat', 'bgm.awb', 'voice.awb',
                          'Font.gsf', 'DFHSR4.TTC', 'env/env_global.dat')) {
    if (!(Test-Path -LiteralPath (Join-Path $data $required) -PathType Leaf)) {
      throw "Missing original game file: $required in $data"
    }
  }
  if (!$SaveDirectory) { $SaveDirectory = Join-Path $PSScriptRoot 'saves/saekano' }
  $save = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SaveDirectory)
  # Keep writable PC saves out of the original data tree, including its parent
  # GAMEFILE directory. No save migration or original-file modification occurs.
  $original = Split-Path -Parent $data
  $protectedPaths = @($data)
  if ((Split-Path -Leaf $original) -eq 'GAMEFILE') { $protectedPaths += $original }
  foreach ($protected in $protectedPaths) {
    if ($save.Equals($protected, [StringComparison]::OrdinalIgnoreCase) -or
        $save.StartsWith($protected.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
      throw 'Choose a PC save directory outside the original game-data directory.'
    }
  }
  # Start-Process joins ArgumentList on Windows. Quote paths for the native
  # command-line parser, including trailing backslashes; never invoke a shell.
  function Quote-Argument([string]$value) {
    '"' + (($value -replace '(\\*)"', '$1$1\"') -replace '(\\+)$', '$1$1') + '"'
  }
  $arguments = @('-g', 'saekano', '--usrdir', (Quote-Argument $data),
                  '--savedatapath', (Quote-Argument $save))
  $process = Start-Process -FilePath (Join-Path $PSScriptRoot 'impacto.exe') `
    -ArgumentList $arguments -WorkingDirectory $PSScriptRoot -PassThru
  $process.WaitForExit()
  $process.Refresh()
  exit $process.ExitCode
} catch {
  Write-Host $_.Exception.Message -ForegroundColor Red
  exit 1
}
