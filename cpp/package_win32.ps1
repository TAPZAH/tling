param(
    [string]$BuildDir = "$PSScriptRoot\build-ctranslate2",
    [string]$CTranslate2Dir = "C:\deps\CTranslate2\build-openblas-dnnl\Release",
    [string]$OutputDir = "$PSScriptRoot\portable-lite",
    [switch]$IncludeModels,
    [switch]$MigrateSettings,
    [switch]$CreateZip,
    [switch]$SkipCheck
)

$ErrorActionPreference = "Stop"

function Copy-RequiredFile {
    param(
        [string]$Source,
        [string]$TargetDirectory
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required file was not found: $Source"
    }
    Copy-Item -LiteralPath $Source -Destination $TargetDirectory -Force
}

function Copy-RuntimeDll {
    param(
        [string]$Name,
        [string]$ReleaseDir,
        [string]$FallbackDir,
        [string]$TargetDirectory
    )

    $fromRelease = Join-Path $ReleaseDir $Name
    if (Test-Path -LiteralPath $fromRelease -PathType Leaf) {
        Copy-RequiredFile $fromRelease $TargetDirectory
        return
    }
    if ($FallbackDir) {
        $fromFallback = Join-Path $FallbackDir $Name
        if (Test-Path -LiteralPath $fromFallback -PathType Leaf) {
            Copy-RequiredFile $fromFallback $TargetDirectory
            return
        }
    }
    throw "Required DLL was not found: $Name"
}

function Find-VcRedistRoot {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        return $null
    }
    $install = & $vswhere -latest -products * -property installationPath 2>$null
    if (-not $install) {
        return $null
    }
    $redist = Join-Path $install "VC\Redist\MSVC"
    if (-not (Test-Path -LiteralPath $redist -PathType Container)) {
        return $null
    }
    $versions = Get-ChildItem -LiteralPath $redist -Directory |
        Sort-Object Name -Descending
    foreach ($version in $versions) {
        $crt = Join-Path $version.FullName "x64\Microsoft.VC143.CRT"
        if (Test-Path -LiteralPath $crt -PathType Container) {
            return $version.FullName
        }
    }
    return $null
}

function Copy-VcRedistDlls {
    param([string]$TargetDirectory)

    $required = @(
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "msvcp140.dll",
        "msvcp140_atomic_wait.dll",
        "vcomp140.dll"
    )
    $redistRoot = Find-VcRedistRoot
    $copied = @{}
    if ($redistRoot) {
        $searchDirs = @(
            (Join-Path $redistRoot "x64\Microsoft.VC143.CRT"),
            (Join-Path $redistRoot "x64\Microsoft.VC143.OpenMP"),
            (Join-Path $redistRoot "x64\Microsoft.VC143.OPENMP")
        )
        foreach ($name in $required) {
            foreach ($dir in $searchDirs) {
                $path = Join-Path $dir $name
                if (Test-Path -LiteralPath $path -PathType Leaf) {
                    Copy-RequiredFile $path $TargetDirectory
                    $copied[$name] = $true
                    break
                }
            }
        }
    }
    foreach ($name in $required) {
        if ($copied.ContainsKey($name)) {
            continue
        }
        $system = Join-Path $env:SystemRoot "System32\$name"
        if (Test-Path -LiteralPath $system -PathType Leaf) {
            Write-Warning "VC DLL взята из System32 (нет каталога VS Redist): $name"
            Copy-RequiredFile $system $TargetDirectory
            $copied[$name] = $true
        }
    }
    $missing = $required | Where-Object { -not $copied.ContainsKey($_) }
    if ($missing) {
        throw "Не найдены распространяемые VC/OpenMP DLL: $($missing -join ', ')"
    }
}

function Copy-TreeExcluding {
    param(
        [string]$Source,
        [string]$Destination,
        [string[]]$ExcludeNames
    )

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force | Where-Object {
        $ExcludeNames -notcontains $_.Name
    } | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName `
            -Destination (Join-Path $Destination $_.Name) `
            -Recurse -Force
    }
}

function Copy-LicenseFiles {
    param(
        [string]$BuildDir,
        [string]$CTranslate2Dir,
        [string]$TargetDirectory
    )

    New-Item -ItemType Directory -Path $TargetDirectory -Force | Out-Null
    $packagingNotice = Join-Path $PSScriptRoot "packaging\THIRD_PARTY.md"
    Copy-RequiredFile $packagingNotice $TargetDirectory

    $repoLicense = Join-Path $PSScriptRoot "..\LICENSE"
    if (Test-Path -LiteralPath $repoLicense -PathType Leaf) {
        Copy-Item -LiteralPath $repoLicense `
            -Destination (Join-Path $TargetDirectory "LICENSE-tling.txt") `
            -Force
    }

    $ct2Candidates = @(
        (Join-Path $CTranslate2Dir "..\..\LICENSE"),
        "C:\deps\CTranslate2\LICENSE"
    )
    foreach ($candidate in $ct2Candidates) {
        $resolved = [System.IO.Path]::GetFullPath($candidate)
        if (Test-Path -LiteralPath $resolved -PathType Leaf) {
            Copy-Item -LiteralPath $resolved `
                -Destination (Join-Path $TargetDirectory "LICENSE-CTranslate2.txt") `
                -Force
            break
        }
    }

    $shareRoots = @(
        (Join-Path $BuildDir "vcpkg_installed\x64-windows\share"),
        "C:\vcpkg\installed\x64-windows\share"
    )
    if ($env:VCPKG_ROOT) {
        $shareRoots += (Join-Path $env:VCPKG_ROOT "installed\x64-windows\share")
    }
    $vcpkgLicenses = @{
        "openblas" = "copyright-OpenBLAS.txt"
        "protobuf" = "copyright-protobuf.txt"
        "abseil" = "copyright-abseil.txt"
        "sentencepiece" = "copyright-SentencePiece.txt"
        "nlohmann-json" = "copyright-nlohmann-json.txt"
        "utf8-range" = "copyright-utf8-range.txt"
    }
    foreach ($entry in $vcpkgLicenses.GetEnumerator()) {
        foreach ($root in $shareRoots) {
            $copyright = Join-Path $root "$($entry.Key)\copyright"
            if (Test-Path -LiteralPath $copyright -PathType Leaf) {
                Copy-Item -LiteralPath $copyright `
                    -Destination (Join-Path $TargetDirectory $entry.Value) `
                    -Force
                break
            }
        }
    }
}

function Write-PortableReadme {
    param(
        [string]$OutputDir,
        [bool]$WithModels
    )

    $template = Join-Path $PSScriptRoot "packaging\README.portable.txt"
    $text = [System.IO.File]::ReadAllText($template, [System.Text.Encoding]::UTF8)
    if ($WithModels) {
        $extra = "В папке data могут лежать модели Argos (argos-packages) и NLLB-200 (nllb-200)."
    } else {
        $extra = "Языковые модели в эту сборку не входят. Скачайте их в окне Пакеты (нужен интернет) либо соберите с -IncludeModels."
    }
    $text = $text.Replace("{{MODELS}}", $extra)
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText((Join-Path $OutputDir "README.txt"), $text, $utf8)
    [System.IO.File]::WriteAllText((Join-Path $OutputDir "Прочитайте.txt"), $text, $utf8)
}

function Write-DataPlaceholder {
    param(
        [string]$DataDir,
        [switch]$MigrateSettings
    )

    New-Item -ItemType Directory -Path $DataDir -Force | Out-Null
    $template = Join-Path $PSScriptRoot "packaging\README.data.txt"
    $note = [System.IO.File]::ReadAllText($template, [System.Text.Encoding]::UTF8)
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText((Join-Path $DataDir "README.txt"), $note, $utf8)

    $settingsTarget = Join-Path $DataDir "settings.json"
    if ($MigrateSettings) {
        $homeEnv = [Environment]::GetEnvironmentVariable("OFFLINE_TRANSLATOR_HOME")
        $candidates = @()
        if ($homeEnv) {
            $candidates += (Join-Path $homeEnv "settings.json")
        }
        $candidates += (Join-Path $HOME ".local\share\offline-translator\settings.json")
        foreach ($source in $candidates) {
            if (Test-Path -LiteralPath $source -PathType Leaf) {
                Copy-Item -LiteralPath $source -Destination $settingsTarget -Force
                Write-Output "Перенесены настройки: $source"
                return
            }
        }
        Write-Output "Настройки в профиле не найдены, data/settings.json не создан"
    }
}

try {
    $vcpkgBin = Join-Path $BuildDir "vcpkg_installed\x64-windows\bin"
    $releaseDir = Join-Path $BuildDir "Release"
    $exe = Join-Path $releaseDir "TLing.exe"
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        throw "Build TLing Release first: $exe"
    }

    if (Test-Path -LiteralPath $OutputDir) {
        Remove-Item -LiteralPath $OutputDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $OutputDir | Out-Null
    Copy-RequiredFile $exe $OutputDir

    Copy-RuntimeDll "ctranslate2.dll" $releaseDir $CTranslate2Dir $OutputDir
    Copy-RuntimeDll "openblas.dll" $releaseDir "C:\vcpkg\installed\x64-windows\bin" $OutputDir
    foreach ($name in @("libprotobuf.dll", "abseil_dll.dll")) {
        Copy-RuntimeDll $name $releaseDir $vcpkgBin $OutputDir
    }
    # oneDNN — int8-бэкенд CTranslate2 для NLLB.
    foreach ($candidate in @(
        (Join-Path $releaseDir "dnnl.dll"),
        "C:\deps\oneDNN\install\bin\dnnl.dll"
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            Copy-RequiredFile $candidate $OutputDir
            break
        }
    }
    # zlib/zstd — распаковка моделей Firefox; fxbridge — движок Firefox.
    foreach ($name in @("z.dll", "zstd.dll", "fxbridge.dll")) {
        if (Test-Path -LiteralPath (Join-Path $releaseDir $name) -PathType Leaf) {
            Copy-RequiredFile (Join-Path $releaseDir $name) $OutputDir
        } else {
            Write-Warning "Нет $name рядом со сборкой — часть функций может не работать"
        }
    }
    Copy-VcRedistDlls -TargetDirectory $OutputDir

    $assetsDir = Join-Path $OutputDir "assets"
    New-Item -ItemType Directory -Path $assetsDir | Out-Null
    $iconSource = Join-Path $PSScriptRoot "..\assets\app.ico"
    Copy-RequiredFile $iconSource $assetsDir
    foreach ($iconName in @("icon.png", "icon-light.png", "icon-dark.png")) {
        $selectionIconSource = Join-Path $PSScriptRoot "..\assets\$iconName"
        if (Test-Path -LiteralPath $selectionIconSource -PathType Leaf) {
            Copy-RequiredFile $selectionIconSource $assetsDir
        } else {
            Write-Warning "Нет assets/$iconName — часть иконок темы будет недоступна"
        }
    }

    $dataDir = Join-Path $OutputDir "data"
    Write-DataPlaceholder -DataDir $dataDir -MigrateSettings:$MigrateSettings

    if ($IncludeModels) {
        $argosSource = Join-Path $HOME ".local\share\argos-translate\packages"
        $nllbSource = Join-Path $HOME ".local\share\offline-translator\nllb-200"
        $exclude = @("_downloads", "stanza")
        if (Test-Path -LiteralPath $argosSource -PathType Container) {
            Copy-TreeExcluding $argosSource (Join-Path $dataDir "argos-packages") $exclude
            Write-Output "Скопированы пакеты Argos"
        } else {
            Write-Output "Нет пакетов Argos: $argosSource"
        }
        if (Test-Path -LiteralPath $nllbSource -PathType Container) {
            Copy-TreeExcluding $nllbSource (Join-Path $dataDir "nllb-200") $exclude
            Write-Output "Скопирована модель NLLB-200"
        } else {
            Write-Output "Нет модели NLLB: $nllbSource"
        }
    }

    Copy-LicenseFiles -BuildDir $BuildDir -CTranslate2Dir $CTranslate2Dir `
        -TargetDirectory (Join-Path $OutputDir "licenses")
    Write-PortableReadme -OutputDir $OutputDir -WithModels:$IncludeModels.IsPresent

    if ($CreateZip) {
        $zipPath = "$OutputDir.zip"
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        Compress-Archive -Path (Join-Path $OutputDir "*") -DestinationPath $zipPath
        Write-Output "Архив: $zipPath"
    }

    Write-Output "Package ready: $OutputDir"

    if (-not $SkipCheck) {
        $checker = Join-Path $PSScriptRoot "check_package.ps1"
        & $checker -PackageDir $OutputDir
        if ($LASTEXITCODE -ne 0) {
            throw "check_package.ps1 завершился с кодом $LASTEXITCODE"
        }
    }
} catch {
    Write-Error "Packaging error: $($_.Exception.Message)"
    exit 1
}
