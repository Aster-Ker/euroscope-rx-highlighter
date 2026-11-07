# One-step build: fetch SDK (if missing), configure, build, run tests.
# Needs Visual Studio 2022 with "Desktop development with C++" (or Build Tools).
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
if (-not (Test-Path (Join-Path $root 'third_party\euroscope\EuroScopePlugInDll.lib'))) {
    & (Join-Path $PSScriptRoot 'fetch-sdk.ps1')
}
cmake -S $root -B (Join-Path $root 'build') -G "Visual Studio 17 2022" -A Win32
cmake --build (Join-Path $root 'build') --config Release
ctest --test-dir (Join-Path $root 'build') -C Release --output-on-failure
Write-Host ""
Write-Host "Built: $(Join-Path $root 'build\Release\RxHighlighter.dll')"
