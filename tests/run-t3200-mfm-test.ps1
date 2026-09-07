param([string] $OutputPath)
$ErrorActionPreference = 'Stop'
$taskRepo = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $taskRepo 'build/t3200-mfm-test.exe' }
$env:Path = 'C:/msys64/ucrt64/bin;C:/msys64/usr/bin;' + $env:Path
& gcc -O2 -std=gnu11 -Wall -Wextra -Werror -Wno-empty-body -ffunction-sections -fdata-sections '-Wl,--gc-sections' `
    -I (Join-Path $taskRepo 'src/include') -I (Join-Path $taskRepo 'build/blumach/src/include') `
    (Join-Path $PSScriptRoot 't3200_mfm_test.c') -o $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'T3200 MFM host test build failed' }
& $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'T3200 MFM host contract failed' }
