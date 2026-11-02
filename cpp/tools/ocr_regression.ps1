# Регресс OCR: CER/WER по эталонным сканам.
# Эталоны лежат рядом с корпусом: samples/ocr-compare/refs/<имя>.txt
# (текст, выверенный по лучшему текущему прогону; правьте вручную при
# необходимости). Запуск:
#   powershell -File cpp\tools\ocr_regression.ps1
#   powershell -File cpp\tools\ocr_regression.ps1 -Accurate
#   powershell -File cpp\tools\ocr_regression.ps1 -UpdateRefs -Accurate
param(
    [string]$BuildDir = "",
    [string]$Models = "$env:USERPROFILE\.local\share\offline-translator\ppocr",
    [switch]$Accurate,
    [switch]$UpdateRefs,
    [double]$MaxCer = 10.0
)

$ErrorActionPreference = "Stop"
$cppRoot = Split-Path $PSScriptRoot -Parent
$repoRoot = Split-Path $cppRoot -Parent
if ([string]::IsNullOrEmpty($BuildDir)) {
    $BuildDir = Join-Path $cppRoot "build"
}
$exe = Join-Path $BuildDir "Release\paddle_ocr_smoke.exe"
$samples = Join-Path $repoRoot "samples\ocr-compare"
$refs = Join-Path $samples "refs"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Не найден paddle_ocr_smoke.exe: $exe (соберите цель paddle_ocr_smoke)"
}
New-Item -ItemType Directory -Path $refs -Force | Out-Null

$cases = @(
    @{ name = "14-standard-fapiao-china"; file = "14-standard-fapiao-china.jpg"; lang = "zh" },
    @{ name = "15-torg-12-waybill-ru"; file = "15-torg-12-waybill-ru.png"; lang = "ru" },
    @{ name = "13-proforma-invoice-merchandising"; file = "13-proforma-invoice-merchandising.jpg"; lang = "en" },
    @{ name = "18-customs-label-de-2023"; file = "18-customs-label-de-2023.jpg"; lang = "en" },
    @{ name = "06-contract-tv-1975"; file = "06-contract-tv-1975.jpg"; lang = "en" }
)

function Get-NormalizedText([string]$text) {
    if ($null -eq $text) {
        return ""
    }
    return ($text -replace "\s+", " ").Trim()
}

function Get-OcrText([string]$outPath) {
    $lines = @{}
    $order = New-Object System.Collections.Generic.List[int]
    foreach ($row in Get-Content -LiteralPath $outPath -Encoding UTF8) {
        $parts = $row -split "`t"
        if ($parts.Count -lt 7) {
            continue
        }
        $line = [int]$parts[0]
        if (-not $lines.ContainsKey($line)) {
            $lines[$line] = New-Object System.Collections.Generic.List[string]
            $order.Add($line)
        }
        $lines[$line].Add($parts[6])
    }
    $builder = New-Object System.Text.StringBuilder
    foreach ($line in $order) {
        [void]$builder.Append(($lines[$line] -join " "))
        [void]$builder.Append(" ")
    }
    return $builder.ToString()
}

function Get-Levenshtein($left, $right) {
    $n = $left.Count
    $m = $right.Count
    if ($n -eq 0) { return $m }
    if ($m -eq 0) { return $n }
    $previous = New-Object int[] ($m + 1)
    $current = New-Object int[] ($m + 1)
    for ($j = 0; $j -le $m; $j++) { $previous[$j] = $j }
    for ($i = 1; $i -le $n; $i++) {
        $current[0] = $i
        for ($j = 1; $j -le $m; $j++) {
            $cost = 1
            if ($left[$i - 1] -eq $right[$j - 1]) { $cost = 0 }
            $deleted = $previous[$j] + 1
            $inserted = $current[$j - 1] + 1
            $changed = $previous[$j - 1] + $cost
            $best = [Math]::Min($deleted, [Math]::Min($inserted, $changed))
            $current[$j] = $best
        }
        $swap = $previous
        $previous = $current
        $current = $swap
    }
    return $previous[$m]
}

$mode = "fast"
if ($Accurate) { $mode = "accurate" }
if ($Accurate) {
    $env:TLING_OCR_ACCURATE = "1"
} else {
    Remove-Item Env:TLING_OCR_ACCURATE -ErrorAction SilentlyContinue
}

Write-Output "Режим: $mode; модели: $Models"
$failures = 0
foreach ($case in $cases) {
    $image = Join-Path $samples $case.file
    $refPath = Join-Path $refs ($case.name + ".txt")
    $outPath = Join-Path $env:TEMP ("ocr-regress-" + $case.name + ".txt")
    if (-not (Test-Path -LiteralPath $image -PathType Leaf)) {
        Write-Output ("{0}: НЕТ СКАНА {1}" -f $case.name, $image)
        $failures += 1
        continue
    }
    & $exe $Models $image $case.lang $outPath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Output ("{0}: OCR завершился с ошибкой" -f $case.name)
        $failures += 1
        continue
    }
    $actual = Get-NormalizedText (Get-OcrText $outPath)
    if ($UpdateRefs) {
        Set-Content -LiteralPath $refPath -Value $actual -Encoding UTF8
        Write-Output ("{0}: эталон обновлён ({1} символов)" -f $case.name, $actual.Length)
        continue
    }
    if (-not (Test-Path -LiteralPath $refPath -PathType Leaf)) {
        Write-Output ("{0}: нет эталона {1} (запустите с -UpdateRefs)" -f $case.name, $refPath)
        $failures += 1
        continue
    }
    $expected = Get-NormalizedText (Get-Content -LiteralPath $refPath -Encoding UTF8 -Raw)
    $distance = Get-Levenshtein @($expected.ToCharArray()) @($actual.ToCharArray())
    $cer = 0.0
    if ($expected.Length -gt 0) {
        $cer = 100.0 * $distance / $expected.Length
    }
    $expectedWords = @($expected -split " " | Where-Object { $_ -ne "" })
    $actualWords = @($actual -split " " | Where-Object { $_ -ne "" })
    $wordDistance = Get-Levenshtein $expectedWords $actualWords
    $wer = 0.0
    if ($expectedWords.Count -gt 0) {
        $wer = 100.0 * $wordDistance / $expectedWords.Count
    }
    $status = "OK"
    if ($cer -gt $MaxCer) {
        $status = "FAIL"
        $failures += 1
    }
    Write-Output ("{0}: CER={1:N2}% WER={2:N2}% [{3}]" -f $case.name, $cer, $wer, $status)
}
Remove-Item Env:TLING_OCR_ACCURATE -ErrorAction SilentlyContinue
if ($failures -gt 0 -and -not $UpdateRefs) {
    Write-Output ("Провалов: {0}" -f $failures)
    exit 1
}
Write-Output "Готово"
