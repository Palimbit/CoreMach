param([string] $OutputPath)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $repo 'build/t3200-ags-test.exe' }
$env:Path = 'C:/msys64/ucrt64/bin;C:/msys64/usr/bin;' + $env:Path
& gcc -O2 -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections '-Wl,--gc-sections' `
    -I (Join-Path $repo 'src/include') -I (Join-Path $repo 'src/cpu') `
    -I (Join-Path $repo 'src/codegen') -I (Join-Path $repo 'build/blumach/src/include') `
    (Join-Path $PSScriptRoot 't3200_ags_test.c') (Join-Path $repo 'src/io.c') -o $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'T3200 callback test build failed' }
& $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'T3200 callback contract failed' }
