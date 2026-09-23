[CmdletBinding()]
param(
    [string]$BuildPreset = 'vs2026-qt611-release',
    [string]$OutputDirectory = '',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildDirectory = Join-Path $projectRoot 'out/build/vs2026-qt611-release'
$runtimeDirectory = Join-Path $buildDirectory 'app/HaiAISpecMonitor/Release'
$packageDirectory = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $projectRoot 'out/packages'
} else {
    [IO.Path]::GetFullPath($OutputDirectory)
}
$stagingDirectory = Join-Path $packageDirectory 'HaiAISpecMonitor'
$archivePath = Join-Path $packageDirectory 'HaiAISpecMonitor-0.1.0-win-x64.7z'
$installerPath = Join-Path $packageDirectory 'HaiAISpecMonitor-0.1.0-win-x64-Setup.exe'
$sfxConfigPath = Join-Path $packageDirectory 'HaiAISpecMonitor-sfx-config.txt'
$sevenZipSfx = 'C:\Program Files\7-Zip\7z.sfx'
$sevenZip = 'C:\Program Files\7-Zip\7z.exe'

if (-not $SkipBuild) {
    & cmake --preset $BuildPreset
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed: $BuildPreset" }

    & cmake --build --preset $BuildPreset --target HaiAISpecMonitor
    if ($LASTEXITCODE -ne 0) { throw "Release build failed: $BuildPreset" }
}

if (-not (Test-Path -LiteralPath $runtimeDirectory -PathType Container)) {
    throw "Release runtime directory not found: $runtimeDirectory"
}
if (-not (Test-Path -LiteralPath $sevenZipSfx -PathType Leaf)) {
    throw "7-Zip SFX module not found: $sevenZipSfx"
}
if (-not (Test-Path -LiteralPath $sevenZip -PathType Leaf)) {
    throw "7-Zip executable not found: $sevenZip"
}

New-Item -ItemType Directory -Force -Path $packageDirectory | Out-Null
if (Test-Path -LiteralPath $stagingDirectory) {
    Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stagingDirectory | Out-Null

# The build directory is already prepared by windeployqt and the vendor
# deployment rules. Copy that self-contained runtime, but do not package
# machine-local policy/history files or debug symbols.
Get-ChildItem -LiteralPath $runtimeDirectory -Force | Where-Object {
    $_.Name -notin @('config') -and $_.Extension -notin @('.pdb', '.ilk', '.exp', '.lib')
} | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $stagingDirectory -Recurse -Force
}

if (-not (Test-Path -LiteralPath (Join-Path $stagingDirectory 'HaiAISpecMonitor.exe') -PathType Leaf)) {
    throw 'The staged Release executable is missing.'
}
if (-not (Test-Path -LiteralPath (Join-Path $stagingDirectory 'models/scn_model.engine') -PathType Leaf)) {
    throw 'The staged TensorRT model is missing.'
}

if (Test-Path -LiteralPath $archivePath) { Remove-Item -LiteralPath $archivePath -Force }
& $sevenZip a -t7z -mx=5 $archivePath (Join-Path $stagingDirectory '*') | Out-Host
if ($LASTEXITCODE -ne 0) { throw "7-Zip archive creation failed: $archivePath" }

$sfxConfig = @"
;!@Install@!UTF-8!
Title="HaiAISpecMonitor 0.1.0"
InstallPath="%ProgramFiles%\SCN\HaiAISpecMonitor"
RunProgram="HaiAISpecMonitor.exe"
;!@InstallEnd@!
"@
[IO.File]::WriteAllText($sfxConfigPath, $sfxConfig, [Text.UTF8Encoding]::new($false))

if (Test-Path -LiteralPath $installerPath) { Remove-Item -LiteralPath $installerPath -Force }
$sfxBytes = [IO.File]::ReadAllBytes($sevenZipSfx)
$configBytes = [IO.File]::ReadAllBytes($sfxConfigPath)
$archiveBytes = [IO.File]::ReadAllBytes($archivePath)
$allBytes = [byte[]]::new($sfxBytes.Length + $configBytes.Length + $archiveBytes.Length)
[Array]::Copy($sfxBytes, 0, $allBytes, 0, $sfxBytes.Length)
[Array]::Copy($configBytes, 0, $allBytes, $sfxBytes.Length, $configBytes.Length)
[Array]::Copy($archiveBytes, 0, $allBytes, $sfxBytes.Length + $configBytes.Length, $archiveBytes.Length)
[IO.File]::WriteAllBytes($installerPath, $allBytes)

Remove-Item -LiteralPath $sfxConfigPath -Force
Remove-Item -LiteralPath $archivePath -Force
Remove-Item -LiteralPath $stagingDirectory -Recurse -Force

$hash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item -LiteralPath $installerPath).Length
[pscustomobject]@{
    Installer = $installerPath
    SizeBytes = $size
    Sha256 = $hash
} | Format-List
