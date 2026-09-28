# Полный релизный конвейер.
#   1) портатив без моделей -> cpp/portable-lite.zip, копия в release/
#   2) установщик без моделей -> release/ (основной файл для GitHub)
#   3) портатив с моделями  -> cpp/portable-full.zip, копия в release/
#   4) установщик с моделями -> release/
#   5) src.zip              -> release/ (git archive HEAD)
param(
    [string]$BuildDir = "$PSScriptRoot\build-ctranslate2",
    [string]$OutputRoot = (Join-Path $PSScriptRoot "..\release")
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent

# Версия из app_version.hpp: kAppVersion = "0.996-beta";
$header = Get-Content (Join-Path $PSScriptRoot "include\offline_translator\app_version.hpp") -Raw
if ($header -notmatch 'kAppVersion\s*=\s*"([^"]+)"') {
    throw "Не удалось прочитать версию из app_version.hpp"
}
$version = $Matches[1]
Write-Output "Версия: $version"

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
# Чистим артефакты этой же версии, чтобы не смешивать старые сборки.
$stale = Get-ChildItem $OutputRoot -Filter "tling-$version-*" -ErrorAction SilentlyContinue
if ($stale) {
    $stale | Remove-Item -Recurse -Force
}

$pack = Join-Path $PSScriptRoot "package_win32.ps1"
$iscc = "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
if (-not (Test-Path -LiteralPath $iscc -PathType Leaf)) {
    $iscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
}
if (-not (Test-Path -LiteralPath $iscc -PathType Leaf)) {
    throw "ISCC.exe не найден — установите Inno Setup 6"
}

Write-Output "=== 1. Портатив без моделей ==="
& powershell -ExecutionPolicy Bypass -File $pack -BuildDir $BuildDir `
    -OutputDir (Join-Path $PSScriptRoot "portable-lite") -CreateZip
if ($LASTEXITCODE -ne 0) { throw "package_win32.ps1 (lite) завершился с кодом $LASTEXITCODE" }

Write-Output "=== 2. Установщик без моделей ==="
& $iscc (Join-Path $PSScriptRoot "TLing.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC завершился с кодом $LASTEXITCODE" }
$setupSource = Get-ChildItem (Join-Path $PSScriptRoot "installer-output") -Filter "tling-$version-setup.exe" |
    Select-Object -First 1
if (-not $setupSource) { throw "Установщик без моделей не найден в installer-output" }
Copy-Item $setupSource.FullName (Join-Path $OutputRoot "tling-$version-setup.exe")
Copy-Item (Join-Path $PSScriptRoot "portable-lite.zip") `
    (Join-Path $OutputRoot "tling-$version-portable.zip")

Write-Output "=== 3. Портатив с моделями ==="
& powershell -ExecutionPolicy Bypass -File $pack -BuildDir $BuildDir `
    -OutputDir (Join-Path $PSScriptRoot "portable-full") -IncludeModels -CreateZip
if ($LASTEXITCODE -ne 0) { throw "package_win32.ps1 (full) завершился с кодом $LASTEXITCODE" }
Copy-Item (Join-Path $PSScriptRoot "portable-full.zip") `
    (Join-Path $OutputRoot "tling-$version-portable-with-models.zip")

Write-Output "=== 4. Установщик с моделями ==="
& $iscc /DUSE_FULL_PORTABLE (Join-Path $PSScriptRoot "TLing.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC (full) завершился с кодом $LASTEXITCODE" }
$fullSetup = Get-ChildItem (Join-Path $PSScriptRoot "installer-output") -Filter "tling-$version-with-models-setup.exe" |
    Select-Object -First 1
if (-not $fullSetup) { throw "Установщик с моделями не найден в installer-output" }
Copy-Item $fullSetup.FullName (Join-Path $OutputRoot "tling-$version-setup-with-models.exe")

Write-Output "=== 5. Исходники ==="
& git -C $repoRoot archive --format=zip -o (Join-Path $OutputRoot "tling-$version-src.zip") HEAD
if ($LASTEXITCODE -ne 0) { throw "git archive завершился с кодом $LASTEXITCODE" }

Write-Output "=== Готово ($version) ==="
Get-ChildItem $OutputRoot -Filter "tling-$version-*" |
    Select-Object Name, @{n = "MB"; e = { [math]::Round($_.Length / 1MB, 1) }}
