[CmdletBinding()]
param(
    [ValidateSet('all', 'light', 'imu', 'power')]
    [string]$Mode = 'all'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$idfProfile = 'C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1'

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    if (-not (Test-Path -LiteralPath $idfProfile)) {
        throw "ESP-IDF 5.5.5 is not active and the expected profile was not found: $idfProfile"
    }
    . $idfProfile
}

Push-Location $projectRoot
try {
    $buildDirectory = switch ($Mode) {
        'light' { 'build-wokwi-light' }
        'imu' { 'build-wokwi-imu' }
        'power' { 'build-wokwi-power' }
        default { 'build-wokwi' }
    }
    $modeDefaults = switch ($Mode) {
        'light' { 'simulations/light_only/sdkconfig.defaults' }
        'imu' { 'simulations/imu_only/sdkconfig.defaults' }
        'power' { 'simulations/power_only/sdkconfig.defaults' }
        default { $null }
    }
    $sdkconfigDefaults = 'sdkconfig.defaults;sdkconfig.wokwi.defaults'
    if ($modeDefaults) {
        $sdkconfigDefaults = "$sdkconfigDefaults;$modeDefaults"
    }

    $arguments = @(
        '-B', $buildDirectory,
        "-DSDKCONFIG=$buildDirectory/sdkconfig",
        "-DSDKCONFIG_DEFAULTS=$sdkconfigDefaults",
        'build'
    )
    & idf.py @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ESP-IDF Wokwi build failed with exit code $LASTEXITCODE."
    }

    $flasherArgs = Join-Path $projectRoot "$buildDirectory\flasher_args.json"
    $elf = Join-Path $projectRoot "$buildDirectory\solaryou_sensor.elf"
    if (-not (Test-Path -LiteralPath $flasherArgs) -or
        -not (Test-Path -LiteralPath $elf)) {
        throw 'Build completed without the Wokwi firmware artifacts.'
    }
    $diagram = switch ($Mode) {
        'light' { 'simulations/light_only/diagram.json' }
        'imu' { 'simulations/imu_only/diagram.json' }
        'power' { 'simulations/power_only/diagram.json' }
        default { 'diagram.json' }
    }
    Write-Host "Wokwi $Mode firmware is ready. Open $diagram and start Wokwi."
}
finally {
    Pop-Location
}
