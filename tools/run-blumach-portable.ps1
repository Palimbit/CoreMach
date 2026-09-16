# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [string]$Executable = "build/portable/apps/qt-portable/BluMach-portable.exe",
    [string]$Machine,
    [string[]]$Asset
)

$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Resolve-WorkspacePath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) {
        $candidate = [IO.Path]::GetFullPath($Path)
    }
    else {
        $candidate = [IO.Path]::GetFullPath((Join-Path $sourceRoot $Path))
    }
    if (-not (Test-Path -LiteralPath $candidate)) {
        throw "Path does not exist: $candidate"
    }
    return $candidate
}

$executablePath = Resolve-WorkspacePath $Executable
$ucrtBin = "C:\msys64\ucrt64\bin"
if (-not (Test-Path -LiteralPath $ucrtBin)) {
    throw "The MSYS2 UCRT64 runtime was not found at $ucrtBin"
}
$env:PATH = "$ucrtBin;$env:PATH"

$arguments = @()
if ($Machine) {
    $arguments += @("--machine", $Machine)
}
foreach ($binding in $Asset) {
    if (-not $binding.Contains("=")) {
        throw "Asset bindings use role=path: $binding"
    }
    $arguments += @("--asset", $binding)
}

& $executablePath @arguments
exit $LASTEXITCODE
