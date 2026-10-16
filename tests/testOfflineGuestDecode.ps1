param(
    [Parameter(Mandatory)][string]$Executable,
    [string]$ArtifactsDirectory
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw 'Missing decoder executable.' }
if (-not $ArtifactsDirectory) {
    $ArtifactsDirectory = Join-Path ([IO.Path]::GetTempPath()) ('kyty-guest-decode-' + [Guid]::NewGuid().ToString('N'))
}
if (Test-Path -LiteralPath $ArtifactsDirectory) { throw 'Smoke output directory must be new.' }
New-Item -ItemType Directory -Path $ArtifactsDirectory | Out-Null

function Write-DecoderWords([string]$Name, [string[]]$HexWords) {
    [byte[]]$encoded = foreach ($hexWord in $HexWords) {
        [BitConverter]::GetBytes([Convert]::ToUInt32($hexWord, 16))
    }
    $path = Join-Path $ArtifactsDirectory ($Name + '.bin')
    [IO.File]::WriteAllBytes($path, $encoded)
    return $path
}

function Run-DecoderCase([string]$Name, [string]$Path, [int]$ExpectedExit, [string[]]$Patterns = @()) {
    $outFile = Join-Path $ArtifactsDirectory ($Name + '.out')
    $errFile = Join-Path $ArtifactsDirectory ($Name + '.err')
    $arguments = @('--decode-guest-shader')
    if ($Path) { $arguments += '"' + $Path + '"' }
    $launch = @{
        FilePath = $Executable
        ArgumentList = $arguments
        RedirectStandardOutput = $outFile
        RedirectStandardError = $errFile
        PassThru = $true
    }
    if ($IsWindows) { $launch.WindowStyle = 'Hidden' }
    $process = Start-Process @launch
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "$Name timed out."
    }
    if ($process.ExitCode -ne $ExpectedExit) {
        throw "$Name returned $($process.ExitCode), expected $ExpectedExit. See $errFile"
    }
    $stdout = [IO.File]::ReadAllText($outFile)
    foreach ($pattern in $Patterns) {
        if ($stdout -notmatch $pattern) { throw "$Name missing expected decoder text: $pattern" }
    }
    if ($stdout -match 'VulkanHarness|\[gpu\]|\[host\]') {
        throw "$Name entered an unrelated harness path."
    }
    Write-Output "[offline-decode] $Name ok"
}

# Encodings use the existing EncodeSMovB32/EncodeSopk fixtures:
# S_MOV_B32 s0,literal; literal; S_WAITCNT_VSCNT s4,0x9234; S_ENDPGM.
$positive = Write-DecoderWords 'literal-wait' @('BE8003FF','12345678','BB849234','BF810000')
Run-DecoderCase 'literal-wait' $positive 0 @('S_MOV_B32','0x12345678','S_WAITCNT_VSCNT','S_ENDPGM','0x0000000c')
$branch = Write-DecoderWords 'valid-branch' @('BF820000','BF810000')
Run-DecoderCase 'valid-branch' $branch 0 @('S_BRANCH','S_ENDPGM')
$empty = Join-Path $ArtifactsDirectory 'empty.bin'
[IO.File]::WriteAllBytes($empty, [byte[]]@())
Run-DecoderCase 'empty' $empty 2
$unaligned = Join-Path $ArtifactsDirectory 'unaligned.bin'
[IO.File]::WriteAllBytes($unaligned, [byte[]]@(1,2,3))
Run-DecoderCase 'unaligned' $unaligned 2
Run-DecoderCase 'missing-input' (Join-Path $ArtifactsDirectory 'absent.bin') 2
Run-DecoderCase 'missing-argument' '' 2
Run-DecoderCase 'truncated-literal' (Write-DecoderWords 'truncated-literal' @('BE8003FF')) 2
Run-DecoderCase 'truncated-smem' (Write-DecoderWords 'truncated-smem' @('F4000100')) 2
Run-DecoderCase 'missing-end' (Write-DecoderWords 'missing-end' @('BE800380')) 2
Run-DecoderCase 'invalid-branch' (Write-DecoderWords 'invalid-branch' @('BF820005','BF810000')) 2
Run-DecoderCase 'unknown-family' (Write-DecoderWords 'unknown-family' @('C0000000','BF810000')) 2
$oversized = Join-Path $ArtifactsDirectory 'oversized.bin'
$sizedFile = [IO.File]::Create($oversized)
try { $sizedFile.SetLength(16MB + 4) } finally { $sizedFile.Dispose() }
Run-DecoderCase 'oversized' $oversized 2
Write-Output "[offline-decode] all 12 cases passed; artifacts: $ArtifactsDirectory"
