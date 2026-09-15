param(
    [string]$Version = "0.1.0-rc.1",
    [string]$Configuration = "Release",
    [switch]$SkipArchive
)

$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$bash = "C:\msys64\usr\bin\bash.exe"
if (-not (Test-Path -LiteralPath $bash)) {
    throw "MSYS2 was not found at $bash"
}

Push-Location $sourceRoot
try {
    $reportedVersion = python -B tools/release_version.py check
    if ($LASTEXITCODE -ne 0 -or $reportedVersion -notmatch [regex]::Escape($Version)) {
        throw "Release metadata does not describe BluMach $Version"
    }

    $changes = git status --porcelain --untracked-files=normal
    if ($changes) {
        throw "The RC must be built from a clean, committed checkout."
    }

    $buildRelative = "build/private-rc-$Version"
    $stageRelative = "dist/staging/BluMach-$Version-Windows-x86_64"
    $outputRelative = "dist/BluMach-$Version-Windows-x86_64"
    $stagePath = Join-Path $sourceRoot $stageRelative
    $outputBase = Join-Path $sourceRoot $outputRelative

    foreach ($path in @($stagePath, "$outputBase.zip", "$outputBase.manifest.json", "$outputBase.zip.sha256")) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }

    $env:MSYSTEM = "UCRT64"
    $env:CHERE_INVOKING = "1"
    $env:BLUMACH_RC_SOURCE = $sourceRoot
    $env:BLUMACH_RC_SOURCE_UNIX = (& $bash -lc 'cygpath -u "$BLUMACH_RC_SOURCE"').Trim()
    if ($LASTEXITCODE -ne 0 -or -not $env:BLUMACH_RC_SOURCE_UNIX) {
        throw "The source path could not be converted for MSYS2."
    }
    $env:BLUMACH_RC_BUILD = $buildRelative
    $env:BLUMACH_RC_STAGE = $stageRelative
    $env:BLUMACH_RC_CONFIGURATION = $Configuration
    $env:BLUMACH_RC_VERSION = $Version

    $buildCommands = @'
set -euo pipefail
cd "$BLUMACH_RC_SOURCE_UNIX"
cmake -S . -B "$BLUMACH_RC_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BLUMACH_RC_CONFIGURATION" \
  -DCMAKE_INSTALL_PREFIX="$BLUMACH_RC_STAGE" \
  -DRELEASE=ON -DNEW_DYNAREC=ON -DQT=ON -DSTATIC_BUILD=OFF
cmake --build "$BLUMACH_RC_BUILD"
ctest --test-dir "$BLUMACH_RC_BUILD" --output-on-failure
cmake --install "$BLUMACH_RC_BUILD"
python -B tools/package_audit.py \
  --root "$BLUMACH_RC_STAGE" --platform windows --qt on \
  --max-size-mib 260 --expected-version "$BLUMACH_RC_VERSION"
'@
    & $bash -lc $buildCommands
    if ($LASTEXITCODE -ne 0) {
        throw "The UCRT64 build or validation failed."
    }

    python -B tools/rc_manifest.py `
        --root $stagePath `
        --source $sourceRoot `
        --version $Version `
        --platform windows-x86_64 `
        --configuration $Configuration `
        --output "$outputBase.manifest.json"
    if ($LASTEXITCODE -ne 0) {
        throw "The RC manifest could not be created."
    }

    if (-not $SkipArchive) {
        Compress-Archive -Path (Join-Path $stagePath "*") -DestinationPath "$outputBase.zip"
        $archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath "$outputBase.zip").Hash.ToLowerInvariant()
        [IO.File]::WriteAllText(
            "$outputBase.zip.sha256",
            "$archiveHash  BluMach-$Version-Windows-x86_64.zip`n",
            [Text.UTF8Encoding]::new($false)
        )
        Write-Host "Private RC: $outputBase.zip"
        Write-Host "SHA-256:    $archiveHash"
    }
    Write-Host "Manifest:   $outputBase.manifest.json"
}
finally {
    Pop-Location
}
