# Downloads the EuroScope plug-in SDK files (EuroScopePlugIn.h and
# EuroScopePlugInDll.lib) into third_party/euroscope.
#
# They are taken from the VATSIM UK uk-controller-plugin repository, so this
# plugin is compiled against exactly the same SDK as UK Controller Plugin.
# Alternatively copy the two files from your EuroScope plug-in SDK by hand.
$ErrorActionPreference = 'Stop'
$base = 'https://raw.githubusercontent.com/VATSIM-UK/uk-controller-plugin/main'
$dest = Join-Path $PSScriptRoot '..\third_party\euroscope'
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Invoke-WebRequest "$base/third_party/euroscope/EuroScopePlugIn.h" -OutFile (Join-Path $dest 'EuroScopePlugIn.h')
Invoke-WebRequest "$base/lib/EuroScopePlugInDll.lib" -OutFile (Join-Path $dest 'EuroScopePlugInDll.lib')
Write-Host "EuroScope SDK downloaded to $dest"
