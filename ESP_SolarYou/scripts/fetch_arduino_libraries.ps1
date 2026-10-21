[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$downloadRoot = Join-Path $projectRoot '.downloads'

$libraries = @(
    @{
        Name = 'Adafruit_BNO08x'
        Version = '1.2.7'
        Url = 'https://github.com/adafruit/Adafruit_BNO08x/archive/refs/tags/1.2.7.zip'
        Component = 'adafruit_bno08x'
        Extracted = 'Adafruit_BNO08x-1.2.7'
    },
    @{
        Name = 'Adafruit_INA219'
        Version = '1.2.3'
        Url = 'https://github.com/adafruit/Adafruit_INA219/archive/refs/tags/1.2.3.zip'
        Component = 'adafruit_ina219'
        Extracted = 'Adafruit_INA219-1.2.3'
    },
    @{
        Name = 'Adafruit_BusIO'
        Version = '1.17.4'
        Url = 'https://github.com/adafruit/Adafruit_BusIO/archive/refs/tags/1.17.4.zip'
        Component = 'adafruit_busio'
        Extracted = 'Adafruit_BusIO-1.17.4'
    },
    @{
        Name = 'Adafruit_Sensor'
        Version = '1.1.15'
        Url = 'https://github.com/adafruit/Adafruit_Sensor/archive/refs/tags/1.1.15.zip'
        Component = 'adafruit_unified_sensor'
        Extracted = 'Adafruit_Sensor-1.1.15'
    }
)

New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null

foreach ($library in $libraries) {
    $componentDir = Join-Path $projectRoot (Join-Path 'components' $library.Component)
    $vendorDir = Join-Path $componentDir 'vendor'
    $versionFile = Join-Path $vendorDir '.dependency-version'
    $expected = "$($library.Name) $($library.Version)"

    if (Test-Path -LiteralPath $versionFile) {
        $installed = (Get-Content -Raw -LiteralPath $versionFile).Trim()
        if ($installed -eq $expected) {
            Write-Host "Already present: $expected"
            continue
        }
        throw "Version mismatch in $vendorDir. Remove that vendor directory and rerun this script."
    }
    if (Test-Path -LiteralPath $vendorDir) {
        throw "Unversioned vendor directory exists at $vendorDir. Inspect/remove it before rerunning."
    }

    $archive = Join-Path $downloadRoot "$($library.Name)-$($library.Version).zip"
    $extractParent = Join-Path $downloadRoot "$($library.Name)-$($library.Version)"
    Write-Host "Downloading $expected from the official Adafruit repository..."
    Invoke-WebRequest -UseBasicParsing -Uri $library.Url -OutFile $archive
    Expand-Archive -LiteralPath $archive -DestinationPath $extractParent -Force

    $sourceDir = Join-Path $extractParent $library.Extracted
    if (-not (Test-Path -LiteralPath $sourceDir)) {
        throw "Expected extracted directory not found: $sourceDir"
    }
    New-Item -ItemType Directory -Force -Path $vendorDir | Out-Null
    Copy-Item -Path (Join-Path $sourceDir '*') -Destination $vendorDir -Recurse
    Set-Content -LiteralPath $versionFile -Value $expected -NoNewline
    Write-Host "Installed $expected"
}

Write-Host 'All pinned Arduino libraries are ready.'

