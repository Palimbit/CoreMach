param([string] $OutputPath)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $repo 'build/toshiba-aform-slot-test.exe' }
$testTemp = Join-Path $repo 'build/test-tmp'
New-Item -ItemType Directory -Force -Path $testTemp | Out-Null
$env:TEMP = $testTemp
$env:TMP = $testTemp
$env:Path = 'C:/msys64/ucrt64/bin;C:/msys64/usr/bin;' + $env:Path
& gcc -O2 -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections '-Wl,--gc-sections' `
    -I (Join-Path $repo 'src/include') `
    (Join-Path $PSScriptRoot 'toshiba_aform_slot_test.c') -o $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'Toshiba A-form-factor slot test build failed' }
& $OutputPath
if ($LASTEXITCODE -ne 0) { throw 'Toshiba A-form-factor slot contract failed' }
