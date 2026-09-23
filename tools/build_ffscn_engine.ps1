[CmdletBinding()]
param(
    [string]$Onnx = '',
    [string]$TensorRtExec = 'D:\profiles\HaiAISpecMonitor\trtexec.exe',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$onnxPath = if ([string]::IsNullOrWhiteSpace($Onnx)) {
    Join-Path $projectRoot 'models\ffscn_17.onnx'
} else { [IO.Path]::GetFullPath($Onnx) }
$outputRoot = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $projectRoot 'models'
} else { [IO.Path]::GetFullPath($OutputDirectory) }
$onnxPath = (Resolve-Path -LiteralPath $onnxPath).Path
$trtexecPath = (Resolve-Path -LiteralPath $TensorRtExec).Path
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$common = @(
    "--onnx=$onnxPath",
    '--minShapes=spectrum:1x1x10x8192',
    '--optShapes=spectrum:1x1x10x131072',
    '--maxShapes=spectrum:1x1x10x131072',
    '--profilingVerbosity=detailed',
    '--skipInference'
)
$fp32 = Join-Path $outputRoot 'ffscn_17_fp32.engine'
& $trtexecPath @common '--noTF32' "--saveEngine=$fp32"
if ($LASTEXITCODE -ne 0) { throw "TensorRT FP32 engine build failed (exit $LASTEXITCODE)." }
$fp16 = Join-Path $outputRoot 'ffscn_17_fp16_candidate.engine'
& $trtexecPath @common '--noTF32' '--fp16' "--saveEngine=$fp16"
if ($LASTEXITCODE -ne 0) { throw "TensorRT FP16 engine build failed (exit $LASTEXITCODE)." }

# FP32 is the safe deployment choice until fixed-sample decoded FP16 parity is
# explicitly accepted. A validated FP16 artifact can replace it after review.
$deploymentEngine = Join-Path $outputRoot 'ffscn_17.engine'
Copy-Item -LiteralPath $fp32 -Destination $deploymentEngine -Force

$manifestPath = Join-Path $outputRoot 'ffscn_17.manifest.json'
$entries = @()
foreach ($candidate in @(@{Name='FP32'; Path=$fp32}, @{Name='FP16'; Path=$fp16})) {
    $file = Get-Item -LiteralPath $candidate.Path
    $hash = (Get-FileHash -LiteralPath $candidate.Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $entries += [pscustomobject]@{ precision=$candidate.Name; path=$file.Name; bytes=$file.Length; sha256=$hash }
}
$manifest = [ordered]@{
    model='FFSCN 17th-order'; checkpointSha256='7ec171094decd4557abaaf3e295b0fd7397196c67a9249d5593aef00db885da6'
    onnxSha256=(Get-FileHash -LiteralPath $onnxPath -Algorithm SHA256).Hash.ToLowerInvariant()
    architecture=[ordered]@{ downNums=13; backbone='MobileNetV3 Large'; neck='FPN'; neckChannels=64 }
    io=[ordered]@{ inputName='spectrum'; inputShape='[1,1,10,N]'; inputType='float32'; outputNames=@('hm','bw','off'); outputShape='[1,1,1,N/4]'; outputType='float32' }
    profile=[ordered]@{ min='[1,1,10,8192]'; opt='[1,1,10,131072]'; max='[1,1,10,131072]'; allowedWidths=@(8192,16384,32768,65536,131072) }
    tensorRt='10.11.0'; cudaSdk='12.9'; gpu='RTX 5060 Laptop'; buildCandidates=$entries
    deployment=[ordered]@{ precision='FP32'; path='ffscn_17.engine'; sha256=(Get-FileHash -LiteralPath $deploymentEngine -Algorithm SHA256).Hash.ToLowerInvariant() }
    artifactScope='The deployment package contains deployment.path and this manifest; buildCandidates are conversion outputs.'
    note='FP16 is a candidate only; it was not selected because candidate-to-FP32 decoding parity has not been verified.'
}
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Get-Content -LiteralPath $manifestPath
