param(
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot ".."));
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $projectRoot "out\packages\scn-dev-dependencies.zip"
}

$outputPath = [System.IO.Path]::GetFullPath($Output)
$outputDirectory = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
if (Test-Path -LiteralPath $outputPath) {
    Remove-Item -LiteralPath $outputPath -Force
}

$paths = @(
    (Join-Path $projectRoot "third_party"),
    (Join-Path $projectRoot "models\scn_model.engine")
)
$files = @(
    Get-ChildItem -LiteralPath $paths -File -Recurse
)
if ($files.Count -eq 0) {
    throw "No dependency files were found. Populate third_party and models first."
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$inputBytes = ($files | Measure-Object -Property Length -Sum).Sum
$stream = [System.IO.File]::Open(
    $outputPath,
    [System.IO.FileMode]::CreateNew,
    [System.IO.FileAccess]::ReadWrite,
    [System.IO.FileShare]::None)
$archive = New-Object System.IO.Compression.ZipArchive(
    $stream,
    [System.IO.Compression.ZipArchiveMode]::Create,
    $false)

try {
    foreach ($file in $files) {
        $relativePath = [System.IO.Path]::GetRelativePath(
            $projectRoot,
            $file.FullName).Replace("\", "/")
        $entry = $archive.CreateEntry(
            $relativePath,
            [System.IO.Compression.CompressionLevel]::NoCompression)
        $input = [System.IO.File]::OpenRead($file.FullName)
        $outputStream = $entry.Open()
        try {
            $input.CopyTo($outputStream, 1MB)
        }
        finally {
            $outputStream.Dispose()
            $input.Dispose()
        }
    }
}
finally {
    $archive.Dispose()
    $stream.Dispose()
}

$archiveInfo = Get-Item -LiteralPath $outputPath
[PSCustomObject]@{
    Archive = $archiveInfo.FullName
    Files = $files.Count
    InputBytes = $inputBytes
    ArchiveBytes = $archiveInfo.Length
}
