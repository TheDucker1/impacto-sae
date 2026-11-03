param([Parameter(Mandatory=$true)][string]$OutputDirectory,
      [string]$Usrdir, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$buildRoot = Join-Path $repoRoot 'impacto-build/SaekanoPC-x86-Release'
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (Test-Path -LiteralPath $output) { throw 'Choose a fresh package directory.' }
$data = $null
if ($Usrdir) {
  $data = (Resolve-Path -LiteralPath $Usrdir).Path
  if (!(Test-Path -LiteralPath (Join-Path $data 'resource.dat') -PathType Leaf)) {
    throw 'Usrdir must point to the original game USRDIR directory.'
  }
  $protectedPaths = @($data)
  $parent = Split-Path -Parent $data
  if ((Split-Path -Leaf $parent) -eq 'GAMEFILE') { $protectedPaths += $parent }
  foreach ($protected in $protectedPaths) {
    if ($output.Equals($protected, [StringComparison]::OrdinalIgnoreCase) -or
        $output.StartsWith($protected.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
      throw 'Package output must be outside the original data directory.'
    }
  }
}
if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build_pc.ps1') }
$vsRoot = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools'
$compiler = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC/Tools/MSVC') -Directory |
  Sort-Object Name -Descending | Select-Object -First 1
$dumpbin = Join-Path $compiler.FullName 'bin/Hostx64/x86/dumpbin.exe'
$crt = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC/Redist/MSVC') -Directory |
  Sort-Object Name -Descending |
  ForEach-Object { Join-Path $_.FullName 'x86/Microsoft.VC143.CRT' } |
  Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
foreach ($required in @($dumpbin, (Join-Path $buildRoot 'impacto.exe'))) {
  if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing build artifact/tool: $required" }
}
if (!$crt) { throw 'The x86 Microsoft VC143 redistributable directory is missing.' }
$shareRoot = Join-Path $buildRoot 'vcpkg_installed/x86-windows-saekano/share'
$sdkRoot = Join-Path $repoRoot 'impacto-build/deps/live2d-2.1-opengl-x86'
foreach ($required in @($shareRoot, (Join-Path $sdkRoot 'SOURCE.json'))) {
  if (!(Test-Path -LiteralPath $required)) { throw "Missing dependency provenance: $required" }
}
New-Item -ItemType Directory -Path $output | Out-Null
$queue = [Collections.Generic.Queue[string]]::new()
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$system = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
function Copy-Program([string]$source) {
  $headers = & $dumpbin /headers $source
  if ($LASTEXITCODE -ne 0 -or !($headers -match '14C machine')) {
    throw "Package requires an x86 PE image: $source"
  }
  $target = Join-Path $output (Split-Path -Leaf $source)
  Copy-Item -LiteralPath $source -Destination $target
  $queue.Enqueue($target)
}
Copy-Program (Join-Path $buildRoot 'impacto.exe')
while ($queue.Count) {
  $image = $queue.Dequeue()
  if (!$seen.Add((Split-Path -Leaf $image))) { continue }
  $imports = & $dumpbin /dependents $image
  if ($LASTEXITCODE -ne 0) { throw "Cannot inspect imports: $image" }
  foreach ($line in $imports) {
    if ($line -notmatch '^\s+([\w.-]+\.dll)\s*$') { continue }
    $name = $Matches[1]
    if ($seen.Contains($name) -or (Test-Path -LiteralPath (Join-Path $output $name))) { continue }
    $local = Join-Path $buildRoot $name
    $runtime = Join-Path $crt $name
    if (Test-Path -LiteralPath $local -PathType Leaf) { Copy-Program $local }
    elseif (Test-Path -LiteralPath $runtime -PathType Leaf) { Copy-Program $runtime }
    elseif ($name -match '^(api-ms-win-|ext-ms-win-)' -or
            (Test-Path -LiteralPath (Join-Path $env:WINDIR "SysWOW64/$name"))) {
      $null = $system.Add($name)
    } else { throw "Unresolved runtime dependency $name imported by $image" }
  }
}
Copy-Item -LiteralPath (Join-Path $buildRoot 'saekano-launcher') -Destination $output -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'src/shaders') -Destination $output -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'resources') -Destination $output -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'profiles') -Destination $output -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'gamedefinitions.lua') -Destination $output
Copy-Item -LiteralPath (Join-Path $repoRoot 'basepaths.lua.sample') -Destination (Join-Path $output 'basepaths.lua')
foreach ($name in @('Launch-PC.ps1', 'Play-Saekano.cmd')) {
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $output
}
if ($data) { Set-Content -LiteralPath (Join-Path $output 'game-data.path') -Value $data -Encoding UTF8 }
else { Set-Content -LiteralPath (Join-Path $output 'game-data.path') -Value '' -Encoding UTF8 }
Copy-Item -LiteralPath (Join-Path $repoRoot 'doc/saekano.md') `
  -Destination (Join-Path $output 'README.md')
foreach ($name in @('LICENSE', 'THIRDPARTY.md')) {
  Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $output
}
function Copy-Notice([string]$source, [string]$relative) {
  $target = Join-Path $output "licenses/$relative"
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
  Copy-Item -LiteralPath $source -Destination $target
}
foreach ($directory in Get-ChildItem -LiteralPath $shareRoot -Directory) {
  $notice = Join-Path $directory.FullName 'copyright'
  if (Test-Path -LiteralPath $notice -PathType Leaf) { Copy-Notice $notice "vcpkg/$($directory.Name).txt" }
}
foreach ($notice in Get-ChildItem -LiteralPath (Join-Path $repoRoot 'vendor') -File -Recurse |
    Where-Object { $_.Name -match '^(LICENSE|COPYING|COPYRIGHT|NOTICE)(\..*)?$' }) {
  $relative = $notice.FullName.Substring($repoRoot.Length + 1)
  Copy-Notice $notice.FullName $relative
}
foreach ($notice in Get-ChildItem -LiteralPath $sdkRoot -File -Recurse |
    Where-Object { $_.Name -match '^(SOURCE\.json|LICENSE|COPYING|COPYRIGHT|NOTICE)(\..*)?$' }) {
  Copy-Notice $notice.FullName ("live2d/" + $notice.FullName.Substring($sdkRoot.Length + 1))
}
# Preserve SDK copyright headers/provenance without copying the build library.
foreach ($notice in Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'include') -File -Recurse -Filter '*.h') {
  Copy-Notice $notice.FullName ("live2d/include/" + $notice.FullName.Substring($sdkRoot.Length + 9))
}
$revision = & git -C $repoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot record source revision.' }
$dirty = @(& git -C $repoRoot diff --name-only HEAD)
if ($LASTEXITCODE -ne 0) { throw 'Cannot record modified source paths.' }
$files = @(Get-ChildItem -LiteralPath $output -File -Recurse | ForEach-Object {
  [ordered]@{path=$_.FullName.Substring($output.Length + 1).Replace('\','/');
    bytes=$_.Length; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
} | Sort-Object { $_.path })
$manifest = [ordered]@{format=1; architecture='x86'; platform='Windows 10 or newer';
  source_revision=$revision; modified_tracked_paths=$dirty;
  original_game_data_included=$false; dependency_imports_verified=$true;
  system_imports=@($system | Sort-Object); files=$files}
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'package.json') -Encoding UTF8
Write-Host "Created $output ($($files.Count) files; original game data is external)."
