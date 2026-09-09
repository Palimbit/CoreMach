param([string] $OutputPath)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $repo 'build/t5200-display-hotkey-test.exe' }
$env:Path = 'C:/msys64/ucrt64/bin;C:/msys64/usr/bin;' + $env:Path
& gcc -O2 -std=gnu11 -Wall -Wextra -Werror -Wno-ignored-qualifiers `
    -I (Join-Path $repo 'src/include') -I (Join-Path $repo 'src/cpu') `
    -I (Join-Path $repo 'src/codegen') -I (Join-Path $repo 'build/t5200-qt6/src/include') `
    (Join-Path $PSScriptRoot 't5200_display_hotkey_test.c') -o $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'T5200 display-hotkey test build failed' }
& $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'T5200 display-hotkey contract failed' }
