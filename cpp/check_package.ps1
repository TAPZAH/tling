# Проверяет lite-портативную папку: exe, DLL, иконка, лицензии, без кэша сборки.
param(
    [string]$PackageDir = "$PSScriptRoot\portable-lite",
    [switch]$AllowPdb
)

$ErrorActionPreference = "Stop"

function Find-Dumpbin {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $install = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath 2>$null
        if ($install) {
            $found = Get-ChildItem -Path (Join-Path $install "VC\Tools\MSVC") `
                -Recurse -Filter dumpbin.exe -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match '\\Hostx64\\x64\\dumpbin\.exe$' } |
                Select-Object -First 1
            if ($found) {
                return $found.FullName
            }
        }
    }
    $cmd = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

function Test-RequiredLayout {
    param([string]$Directory)

    $required = @(
        "TLing.exe",
        "ctranslate2.dll",
        "openblas.dll",
        "libprotobuf.dll",
        "abseil_dll.dll",
        "vcruntime140.dll",
        "vcruntime140_1.dll",
        "msvcp140.dll",
        "msvcp140_atomic_wait.dll",
        "vcomp140.dll",
        "assets\app.ico",
        "assets\icon-light.png",
        "assets\icon-dark.png",
        "licenses\THIRD_PARTY.md",
        "README.txt"
    )
    $missing = @()
    foreach ($relative in $required) {
        $path = Join-Path $Directory $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $missing += $relative
        }
    }
    $dataDir = Join-Path $Directory "data"
    if (-not (Test-Path -LiteralPath $dataDir -PathType Container)) {
        $missing += "data/"
    }
    if ($missing.Count -gt 0) {
        throw "В поставке нет: $($missing -join ', ') (каталог: $Directory)"
    }
}

function Test-ForbiddenEntries {
    param(
        [string]$Directory,
        [switch]$AllowPdb
    )

    $forbiddenNames = @(
        "CMakeCache.txt",
        "CMakeFiles",
        "vcpkg_installed",
        "ALL_BUILD.vcxproj",
        "ZERO_CHECK.vcxproj"
    )
    $hits = @()
    Get-ChildItem -LiteralPath $Directory -Recurse -Force | ForEach-Object {
        if ($forbiddenNames -contains $_.Name) {
            $hits += $_.FullName
        }
        if ($_.FullName -match '\\build-[^\\]+\\') {
            $hits += $_.FullName
        }
        if (-not $AllowPdb -and $_.Extension -ieq ".pdb") {
            $hits += $_.FullName
        }
    }
    if ($hits.Count -gt 0) {
        throw "В поставке есть кэш сборки или .pdb:`n$($hits -join "`n")"
    }
}

function Get-SystemDllNames {
    return @(
        "kernel32.dll", "kernelbase.dll", "ntdll.dll", "user32.dll", "gdi32.dll",
        "gdi32full.dll", "shell32.dll", "shlwapi.dll", "advapi32.dll", "sechost.dll",
        "ole32.dll", "oleaut32.dll", "combase.dll", "comctl32.dll", "comdlg32.dll",
        "winhttp.dll", "ws2_32.dll", "wsock32.dll", "bcrypt.dll", "bcryptprimitives.dll",
        "crypt32.dll", "cryptbase.dll", "dbghelp.dll", "rpcrt4.dll", "imm32.dll",
        "uxtheme.dll", "dwmapi.dll", "setupapi.dll", "cfgmgr32.dll", "powrprof.dll",
        "iphlpapi.dll", "normaliz.dll", "nsi.dll", "dnsapi.dll", "mswsock.dll",
        "sspicli.dll", "userenv.dll", "profapi.dll", "version.dll", "winmm.dll",
        "msvcrt.dll", "ucrtbase.dll", "ninput.dll", "win32u.dll", "gdiplus.dll"
    )
}

function Get-PeDependents {
    param(
        [string]$Dumpbin,
        [string]$PePath
    )

    $output = & $Dumpbin /DEPENDENTS $PePath 2>&1 | Out-String
    $names = @()
    $inSection = $false
    foreach ($line in ($output -split "`r?`n")) {
        $trim = $line.Trim()
        if ($trim -match "Image has the following dependencies:") {
            $inSection = $true
            continue
        }
        if ($inSection -and ($trim -eq "Summary" -or $trim -like "Summary*")) {
            break
        }
        if ($inSection -and $trim -match '^(?<name>[\w\-.]+\.dll)$') {
            $names += $Matches.name
        }
    }
    return $names
}

function Test-DumpbinClosure {
    param(
        [string]$Directory,
        [string]$Dumpbin
    )

    $system = Get-SystemDllNames
    $packaged = @{}
    Get-ChildItem -LiteralPath $Directory -Filter "*.dll" -File | ForEach-Object {
        $packaged[$_.Name.ToLowerInvariant()] = $true
    }
    Get-ChildItem -LiteralPath $Directory -Filter "*.exe" -File | ForEach-Object {
        $packaged[$_.Name.ToLowerInvariant()] = $true
    }

    $missing = @()
    $delayHits = @()
    $targets = @()
    $targets += Get-ChildItem -LiteralPath $Directory -Filter "*.exe" -File
    $targets += Get-ChildItem -LiteralPath $Directory -Filter "*.dll" -File
    foreach ($pe in $targets) {
        $importsText = & $Dumpbin /IMPORTS $pe.FullName 2>&1 | Out-String
        if ($importsText -match "(?i)delay load") {
            $delayHits += $pe.Name
        }
        foreach ($dep in (Get-PeDependents -Dumpbin $Dumpbin -PePath $pe.FullName)) {
            $lower = $dep.ToLowerInvariant()
            if ($lower.StartsWith("api-ms-win-")) {
                continue
            }
            if ($system -contains $lower) {
                continue
            }
            if (-not $packaged.ContainsKey($lower)) {
                $missing += "$($pe.Name) -> $dep"
            }
        }
    }
    if ($missing.Count -gt 0) {
        throw "dumpbin: не хватает DLL в поставке: $($missing -join '; ')"
    }
    if ($delayHits.Count -gt 0) {
        Write-Warning ("dumpbin: delay-load у {0}; проверьте, что DLL лежат рядом с exe" -f ($delayHits -join ", "))
    }
    Write-Output "dumpbin: импорты закрыты поставленными DLL (без api-ms-win / системных)"
}

try {
    if (-not (Test-Path -LiteralPath $PackageDir -PathType Container)) {
        throw "Каталог поставки не найден: $PackageDir"
    }

    Test-RequiredLayout -Directory $PackageDir
    Test-ForbiddenEntries -Directory $PackageDir -AllowPdb:$AllowPdb

    $dumpbin = Find-Dumpbin
    if ($dumpbin) {
        Test-DumpbinClosure -Directory $PackageDir -Dumpbin $dumpbin
    } else {
        Write-Warning "dumpbin.exe не найден, проверка импортов пропущена"
    }

    Write-Output "Проверка поставки прошла: $PackageDir"
    exit 0
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
