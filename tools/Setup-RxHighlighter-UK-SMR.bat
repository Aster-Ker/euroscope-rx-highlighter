@echo off
setlocal
rem ==========================================================================
rem  RX Highlighter - set up every VATSIM UK SMR profile automatically
rem
rem  * Copies RxHighlighter.dll into UK\Data\Plugin
rem    (uses RxHighlighter.dll next to this file, or downloads the latest
rem     release from GitHub if there isn't one)
rem  * Loads the plug-in in every profile that uses the SMR lists
rem  * Changes every callsign column in the SMR lists to
rem    "RX Highlighter: Callsign (lights up on RX)", keeping the same
rem    mouse-button functions (UKCP / SRD Search / vFPC clicks unchanged)
rem  * Backs up every file it changes as <file>.rxh-backup
rem
rem  Run it again after a UK controller pack update (the updater may
rem  overwrite these files). Undo with Undo-RxHighlighter-UK-SMR.bat.
rem
rem  Close EuroScope before running.
rem ==========================================================================
set "RXH_DIR=%~dp0"
set "RXH_MODE=%~1"
if "%RXH_MODE%"=="" set "RXH_MODE=install"
set "RXH_SELF=%~f0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$m = '#' + 'PS' + 'START'; $t = [IO.File]::ReadAllText($env:RXH_SELF); $i = $t.IndexOf(\"`n\" + $m); Invoke-Expression ('& {' + $t.Substring($i + $m.Length + 1) + \"`n}\")"
echo.
pause
exit /b

#PSSTART
$ErrorActionPreference = 'Stop'
$PluginName = 'RX Highlighter'          # must match the plug-in's registered name
$DllName    = 'RxHighlighter.dll'
$ReleaseUrl = 'https://github.com/Aster-Ker/euroscope-rx-highlighter/releases/latest/download/RxHighlighter.dll'
$Here = $env:RXH_DIR.TrimEnd('\', '/')
$Mode = $env:RXH_MODE.ToLowerInvariant()
$Sep  = [IO.Path]::DirectorySeparatorChar
$Ansi = [Text.Encoding]::Default        # EuroScope files are ANSI

function Say($text, $colour = 'Gray') { Write-Host $text -ForegroundColor $colour }

function Test-UkRoot($path) {
    return $path -and (Test-Path -LiteralPath (Join-Path $path 'Data\Settings\Lists_SMR.txt'))
}

function Find-UkRoot {
    $docs = [Environment]::GetFolderPath('MyDocuments')
    $candidates = @(
        $Here,
        (Join-Path $Here 'UK'),
        (Split-Path $Here -Parent),
        (Join-Path $env:APPDATA 'EuroScope\UK'),
        (Join-Path $docs 'EuroScope\UK')
    )
    foreach ($c in $candidates) { if (Test-UkRoot $c) { return (Resolve-Path -LiteralPath $c).Path } }
    Say "Couldn't find your UK controller pack folder automatically." Yellow
    $answer = (Read-Host 'Paste the full path of your UK folder (the one containing Data and Heathrow)').Trim('"', ' ')
    if (Test-UkRoot $answer) { return (Resolve-Path -LiteralPath $answer).Path }
    throw "That folder doesn't look like the UK controller pack (no Data\Settings\Lists_SMR.txt)."
}

function Read-Lines($path) {
    $text = [IO.File]::ReadAllText($path, $Ansi)
    $nl = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $endsWithNl = $text.EndsWith("`n")
    $lines = [Collections.Generic.List[string]]::new()
    $lines.AddRange([string[]]($text -split "`r?`n"))
    if ($endsWithNl) { $lines.RemoveAt($lines.Count - 1) }
    return [pscustomobject]@{ Lines = $lines; NewLine = $nl; EndsWithNewLine = $endsWithNl }
}

function Write-Lines($path, $doc) {
    $backup = "$path.rxh-backup"
    if (-not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $path -Destination $backup }
    $text = [string]::Join($doc.NewLine, $doc.Lines)
    if ($doc.EndsWithNewLine) { $text += $doc.NewLine }
    [IO.File]::WriteAllText($path, $text, $Ansi)
}

# Turns "\..\Data\Settings\Lists_SMR.txt" (relative to the profile) into a full path.
function Resolve-ProfilePath($profileDir, $relative) {
    $rel = $relative.Replace('\', $Sep).Replace('/', $Sep).TrimStart($Sep)
    return [IO.Path]::GetFullPath((Join-Path $profileDir $rel))
}

if (Get-Process -Name 'EuroScope' -ErrorAction SilentlyContinue) {
    Say 'EuroScope is running. Close it first (it rewrites profiles when it exits), then run this again.' Red
    return
}

$Uk = Find-UkRoot
Say "UK controller pack: $Uk" Cyan

# ---------------------------------------------------------------- undo ----
if ($Mode -eq 'undo') {
    $backups = @(Get-ChildItem -LiteralPath $Uk -Recurse -File -Filter '*.rxh-backup')
    foreach ($b in $backups) {
        $original = $b.FullName.Substring(0, $b.FullName.Length - '.rxh-backup'.Length)
        Copy-Item -LiteralPath $b.FullName -Destination $original -Force
        Remove-Item -LiteralPath $b.FullName
        Say "Restored $($original.Substring($Uk.Length + 1))"
    }
    $dllPath = Join-Path $Uk "Data\Plugin\$DllName"
    if (Test-Path -LiteralPath $dllPath) { Remove-Item -LiteralPath $dllPath; Say "Removed Data\Plugin\$DllName" }
    Say "Done. Restored $($backups.Count) file(s)." Green
    return
}

# ------------------------------------------------------------- install ----
# 1. The DLL
$pluginDir = Join-Path $Uk 'Data\Plugin'
$target = Join-Path $pluginDir $DllName
$local = Join-Path $Here $DllName
if (Test-Path -LiteralPath $local) {
    Copy-Item -LiteralPath $local -Destination $target -Force
    Say "Copied $DllName into Data\Plugin" Green
} else {
    Say 'Downloading the latest RX Highlighter release from GitHub...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $ReleaseUrl -OutFile $target -UseBasicParsing
    Say "Downloaded $DllName into Data\Plugin" Green
}

# 2. Every profile that uses the SMR lists
$listRef = '^Settings\tSettingsfile\w+\t(.*Lists_SMR[^\\/]*\.txt)\s*$'
$profiles = @(Get-ChildItem -LiteralPath $Uk -Recurse -File -Filter '*.prf' |
    Where-Object { [IO.File]::ReadAllText($_.FullName, $Ansi) -match '(?m)^Settings\tSettingsfile\w+\t.*Lists_SMR' })

$listFiles = @{}
$profilesChanged = 0
foreach ($p in $profiles) {
    $doc = Read-Lines $p.FullName
    $lines = $doc.Lines

    foreach ($l in $lines) {
        if ($l -match $listRef) { $listFiles[(Resolve-ProfilePath $p.DirectoryName $Matches[1])] = $true }
    }

    if (@($lines | Where-Object { $_ -match '^Plugins\tPlugin\d+\t.*RxHighlighter\.dll\s*$' }).Count -gt 0) { continue }

    # Use the same relative prefix the profile already uses for UKCP,
    # e.g. "\..\Data\Plugin\" or "\..\..\Data\Plugin\".
    $prefix = $null
    foreach ($l in $lines) {
        if ($l -match '^Plugins\tPlugin\d+\t(.*[\\/]Data[\\/]Plugin[\\/])UKControllerPlugin\.dll\s*$') { $prefix = $Matches[1]; break }
    }
    if (-not $prefix) {
        $relDir = $p.DirectoryName.Substring($Uk.Length).Trim('\', '/')
        $depth = if ($relDir) { ($relDir -split '[\\/]').Count } else { 0 }
        $prefix = ('\..' * $depth) + '\Data\Plugin\'
    }

    $max = -1; $lastPluginLine = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^Plugins\tPlugin(\d+)') {
            $max = [Math]::Max($max, [int]$Matches[1]); $lastPluginLine = $i
        }
    }
    $n = $max + 1
    $new = @(
        "Plugins`tPlugin$n`t$prefix$DllName",
        "Plugins`tPlugin${n}Display0`tStandard ES radar screen",
        "Plugins`tPlugin${n}Display1`tSMR radar display"
    )
    if ($lastPluginLine -ge 0) { $lines.InsertRange($lastPluginLine + 1, [string[]]$new) } else { $lines.AddRange([string[]]$new) }
    Write-Lines $p.FullName $doc
    $profilesChanged++
}
Say "Profiles using the SMR lists: $($profiles.Count) (plug-in added to $profilesChanged)" Green

# 3. Callsign columns in the SMR list files
#    m_Column:<header>:<width>:<centred>:<item>:<left fn>:<right fn>:<flag>:<item plug-in>:<left plug-in>:<right plug-in>:...
#    EuroScope's built-in callsign item is 9; ours is item 1 of "RX Highlighter".
$columnsChanged = 0
foreach ($file in $listFiles.Keys) {
    if (-not (Test-Path -LiteralPath $file)) { Say "Missing list file: $file" Yellow; continue }
    $doc = Read-Lines $file
    $changed = 0
    for ($i = 0; $i -lt $doc.Lines.Count; $i++) {
        $parts = $doc.Lines[$i].Split(':')
        if ($parts.Count -ge 11 -and $parts[0] -eq 'm_Column' -and $parts[4] -eq '9' -and $parts[8] -eq '') {
            $parts[4] = '1'
            $parts[8] = $PluginName
            $doc.Lines[$i] = [string]::Join(':', $parts)
            $changed++
        }
    }
    if ($changed -gt 0) { Write-Lines $file $doc }
    $columnsChanged += $changed
    Say "$($file.Substring($Uk.Length + 1)): $changed callsign column(s) switched"
}

Say ''
Say "Done. $columnsChanged callsign column(s) now light up when the pilot transmits." Green
Say 'Start EuroScope with any UK SMR profile. Test with:  .rxh test <callsign>' Cyan
Say 'Run this again after a UK controller pack update. Undo with Undo-RxHighlighter-UK-SMR.bat.'
