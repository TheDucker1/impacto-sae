param([switch]$ConfigureOnly, [switch]$FreshConfigure, [switch]$Configure,
      [switch]$AllowDownloads,
      [string[]]$Targets = @('impacto'))
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$vsRoot = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools'
$vcvars = Join-Path $vsRoot 'VC/Auxiliary/Build/vcvarsamd64_x86.bat'
$cmakeRoot = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake'
$cmake = Join-Path $cmakeRoot 'CMake/bin/cmake.exe'
$ninja = Join-Path $cmakeRoot 'Ninja/ninja.exe'
$vcpkgRoot = Join-Path $repoRoot 'impacto-build/deps/vcpkg'
$expectedVcpkg = '62159a45e18f3a9ac0548628dcaf74fcb60c6ff9'
$buildRoot = Join-Path $repoRoot 'impacto-build/SaekanoPC-x86-Release'
foreach ($tool in @($vcvars, $cmake, $ninja, (Join-Path $vcpkgRoot 'vcpkg.exe'))) {
  if (!(Test-Path -LiteralPath $tool)) { throw "Missing PC build tool: $tool" }
}
$actualVcpkg = & git -C $vcpkgRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualVcpkg -ne $expectedVcpkg) {
  throw "The PC build requires repository-pinned vcpkg $expectedVcpkg"
}
$vars = & $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'x86 compiler environment initialization failed' }
foreach ($line in $vars) {
  if ($line -match '^([^=]+)=(.*)$') {
    Set-Item -LiteralPath "Env:$($Matches[1])" -Value $Matches[2]
  }
}
# Changes affect only this process. The independent x64 core build is untouched.
$env:VCPKG_ROOT = $vcpkgRoot
$env:VCPKG_DISABLE_METRICS = '1'
$env:VCPKG_MAX_CONCURRENCY = '4'
if ($ConfigureOnly -or $FreshConfigure -or $Configure -or
    !(Test-Path -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt'))) {
  function Find-PkgConfig {
    Get-ChildItem -LiteralPath (Join-Path $vcpkgRoot 'downloads/tools/msys2') `
      -Filter 'pkg-config.exe' -Recurse -ErrorAction SilentlyContinue | `
      Where-Object { $_.FullName -like '*\mingw64\bin\*' } | `
      Select-Object -First 1 -ExpandProperty FullName
  }
  $pkgConfig = Find-PkgConfig
  if (!$pkgConfig) {
    if (!$AllowDownloads) {
      throw 'Native pkg-config is missing. Configure with -AllowDownloads to install dependencies.'
    }
    # The manifest's libass dependency downloads the native pkg-config tool.
    # Install before configure because find_package(PkgConfig) runs before the
    # project's dependency discovery on a fresh machine.
    Push-Location $repoRoot
    try {
      & (Join-Path $vcpkgRoot 'vcpkg.exe') install '--triplet=x86-windows-saekano' `
        "--x-install-root=$buildRoot/vcpkg_installed"
      if ($LASTEXITCODE -ne 0) { throw 'PC dependencies failed to install' }
    } finally { Pop-Location }
    $pkgConfig = Find-PkgConfig
    if (!$pkgConfig) { throw 'The dependency installation did not provide native pkg-config' }
  }
  $env:PKG_CONFIG_PATH = "$buildRoot/vcpkg_installed/x86-windows-saekano/lib/pkgconfig;$buildRoot/vcpkg_installed/x86-windows-saekano/share/pkgconfig"
  [string[]]$freshArgs = @()
  if ($FreshConfigure) { $freshArgs += '--fresh' }
  $manifestInstall = if ($AllowDownloads) { 'ON' } else { 'OFF' }
  $disconnected = if ($AllowDownloads) { 'OFF' } else { 'ON' }
  & $cmake @freshArgs -S $repoRoot -B $buildRoot -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_C_COMPILER=$((Get-Command cl.exe).Source)" `
    "-DCMAKE_CXX_COMPILER=$((Get-Command cl.exe).Source)" `
    '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release' "-DPKG_CONFIG_EXECUTABLE=$pkgConfig" `
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake" `
    '-DVCPKG_TARGET_TRIPLET=x86-windows-saekano' `
    "-DVCPKG_MANIFEST_INSTALL=$manifestInstall" `
    "-DFETCHCONTENT_FULLY_DISCONNECTED=$disconnected" `
    "-DVCPKG_OVERLAY_TRIPLETS=$repoRoot/triplets" `
    '-DIMPACTO_ENABLE_SAEKANO=ON' `
    "-DSAEKANO_CUBISM2_SDK_ROOT=$repoRoot/impacto-build/deps/live2d-2.1-opengl-x86" `
    '-DCMAKE_BUILD_TYPE=Release' '-DIMPACTO_DISABLE_VULKAN=ON' `
    '-DIMPACTO_DISABLE_DX9=ON' '-DIMPACTO_WARNINGS=ON'
  if ($LASTEXITCODE -ne 0) { throw 'PC frontend configuration failed' }
}
if (!$ConfigureOnly) {
  $targetArgs = @()
  if ($Targets.Count) { $targetArgs = @('--target') + $Targets }
  & $cmake --build $buildRoot @targetArgs
  if ($LASTEXITCODE -ne 0) { throw 'PC frontend build failed' }
}
