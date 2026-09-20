# SPDX-License-Identifier: GPL-2.0-or-later
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$FirmwareEven,

    [Parameter(Mandatory = $true)]
    [string]$FirmwareOdd,

    [Parameter(Mandatory = $true)]
    [string]$Floppy,

    [string]$BuildDirectory = "build/pcs86-acceptance",
    [switch]$KeepFrames
)

$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Resolve-InputPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        $candidate = [IO.Path]::GetFullPath($Path)
    }
    else {
        $candidate = [IO.Path]::GetFullPath((Join-Path $sourceRoot $Path))
    }
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Input file does not exist: $candidate"
    }
    return $candidate
}

function Resolve-BuildPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        $candidate = [IO.Path]::GetFullPath($Path)
    }
    else {
        $candidate = [IO.Path]::GetFullPath((Join-Path $sourceRoot $Path))
    }
    if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
        throw "Build directory does not exist: $candidate"
    }
    return $candidate
}

function Assert-Asset(
    [string]$Label,
    [string]$Path,
    [long]$ExpectedSize,
    [string]$ExpectedSha256
) {
    $actualSize = (Get-Item -LiteralPath $Path).Length
    if ($actualSize -ne $ExpectedSize) {
        throw "$Label has size $actualSize; expected $ExpectedSize bytes"
    }
    $actualHash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($actualHash -ne $ExpectedSha256) {
        throw "$Label SHA-256 is $actualHash; expected $ExpectedSha256"
    }
    return $actualHash
}

function Assert-Output([string]$Label, [string]$Output, [string]$Pattern) {
    if ($Output -notmatch $Pattern) {
        throw "$Label did not contain the expected result /$Pattern/.`n$Output"
    }
}

$evenPath = Resolve-InputPath $FirmwareEven
$oddPath = Resolve-InputPath $FirmwareOdd
$floppyPath = Resolve-InputPath $Floppy
$buildPath = Resolve-BuildPath $BuildDirectory

$probePath = Join-Path $buildPath "tests\engine\portable-engine-pcs86-firmware-probe.exe"
$headlessPath = Join-Path $buildPath "apps\headless\BluMach-headless.exe"
foreach ($executable in @($probePath, $headlessPath)) {
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Required executable does not exist: $executable"
    }
}

$expectedHashes = @{
    Even = Assert-Asset "even firmware" $evenPath 32768 "C92A79509DEF8AEE30D76700A5CE1C7B6716D2375A64075C9669CA376FDE4EB8"
    Odd = Assert-Asset "odd firmware" $oddPath 32768 "82F8363EA7CDE1FB8ABE50FD76C7381831B7B89539D3DC71CBE4305FB1568D40"
    Floppy = Assert-Asset "system floppy" $floppyPath 737280 "75E1A068AA5910DB736CE4B53E6B5FC179F83390FF5512D2AF421E57AF3A0C12"
}

$ucrtBin = "C:\msys64\ucrt64\bin"
if (Test-Path -LiteralPath $ucrtBin -PathType Container) {
    $env:PATH = "$ucrtBin;$env:PATH"
}

$temporaryDirectory = Join-Path ([IO.Path]::GetTempPath()) ("blumach-pcs86-acceptance-" + [Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Path $temporaryDirectory
$probeFrame = Join-Path $temporaryDirectory "strict-reference.ppm"
$frontendFrame = Join-Path $temporaryDirectory "default-product.ppm"

try {
    Write-Host "Running strict no-EMS reference (20 virtual seconds)..."
    $probeLines = & $probePath $evenPath $oddPath $probeFrame 20000000000 $floppyPath 2>&1
    $probeExitCode = $LASTEXITCODE
    $probeOutput = $probeLines -join [Environment]::NewLine
    if ($probeExitCode -ne 0) {
        throw "Strict reference exited with code $probeExitCode.`n$probeOutput"
    }
    Assert-Output "strict reference" $probeOutput "status=0"
    Assert-Output "strict reference" $probeOutput "instructions=16503008\b"
    Assert-Output "strict reference" $probeOutput "io=11977\b"
    Assert-Output "strict reference" $probeOutput "crc32=06bd8a15\b"
    Assert-Output "strict reference" $probeOutput "exact=16503008\b"
    Assert-Output "strict reference" $probeOutput "range=0\b"
    Assert-Output "strict reference" $probeOutput "unknown=0\b"

    Write-Host "Running default product profile with 1920 KiB EMS (60 virtual seconds)..."
    $frontendLines = & $headlessPath --machine olivetti-pcs86 `
        --firmware-even $evenPath --firmware-odd $oddPath --floppy $floppyPath `
        --ticks 60000000000 --frame $frontendFrame --expect-frame-crc32 e76fc1da 2>&1
    $frontendExitCode = $LASTEXITCODE
    $frontendOutput = $frontendLines -join [Environment]::NewLine
    if ($frontendExitCode -ne 0) {
        throw "Default product profile exited with code $frontendExitCode.`n$frontendOutput"
    }
    Assert-Output "default product profile" $frontendOutput "status=0"
    Assert-Output "default product profile" $frontendOutput "instructions=51180092\b"
    Assert-Output "default product profile" $frontendOutput "io=152359\b"
    Assert-Output "default product profile" $frontendOutput "crc32=e76fc1da\b"
    Assert-Output "default product profile" $frontendOutput "reads=275\b"
    Assert-Output "default product profile" $frontendOutput "writes=0\b"

    Assert-Asset "even firmware after validation" $evenPath 32768 $expectedHashes.Even | Out-Null
    Assert-Asset "odd firmware after validation" $oddPath 32768 $expectedHashes.Odd | Out-Null
    Assert-Asset "system floppy after validation" $floppyPath 737280 $expectedHashes.Floppy | Out-Null

    Write-Host "PCS 86 portable acceptance passed."
    Write-Host "  Strict reference: 16,503,008 instructions; framebuffer 06bd8a15"
    Write-Host "  Default product: 51,180,092 instructions; 275 floppy reads; framebuffer e76fc1da"
    Write-Host "  Firmware and floppy hashes are unchanged."
    if ($KeepFrames) {
        Write-Host "  Frames: $temporaryDirectory"
    }
}
finally {
    if (-not $KeepFrames) {
        foreach ($frame in @($probeFrame, $frontendFrame)) {
            if (Test-Path -LiteralPath $frame -PathType Leaf) {
                Remove-Item -LiteralPath $frame -Force
            }
        }
        if (Test-Path -LiteralPath $temporaryDirectory -PathType Container) {
            Remove-Item -LiteralPath $temporaryDirectory -Force
        }
    }
}
