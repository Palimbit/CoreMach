param(
    [string]$Executable = "build/artifacts/BluMach.exe",
    [string]$VmPath,
    [string]$LogPath,
    [string]$RomPath = "roms"
)

$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Resolve-WorkspacePath([string]$Path, [switch]$MustExist) {
    if ([IO.Path]::IsPathRooted($Path)) {
        $candidate = [IO.Path]::GetFullPath($Path)
    }
    else {
        $candidate = [IO.Path]::GetFullPath((Join-Path $sourceRoot $Path))
    }
    if ($MustExist -and -not (Test-Path -LiteralPath $candidate)) {
        throw "Path does not exist: $candidate"
    }
    return $candidate
}

$executablePath = Resolve-WorkspacePath $Executable -MustExist
$romPathValue = Resolve-WorkspacePath $RomPath -MustExist
$arguments = @("-R", $romPathValue)
if ($VmPath) {
    $arguments += @("-P", (Resolve-WorkspacePath $VmPath -MustExist))
}
if ($LogPath) {
    $arguments += @("-L", (Resolve-WorkspacePath $LogPath))
}

$ucrtBin = "C:\msys64\ucrt64\bin"
if (-not (Test-Path -LiteralPath $ucrtBin)) {
    throw "The MSYS2 UCRT64 runtime was not found at $ucrtBin"
}
$env:PATH = "$ucrtBin;$env:PATH"

& $executablePath @arguments
exit $LASTEXITCODE
