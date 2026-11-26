@echo off
:: ------------------------------------------------------------------------------
:: prism regression test suite (windows)
::
:: Runs every tests\cases\*.test through the prism headless render executor,
:: compares each render against its ground truth with `prism compare`, and appends
:: one JSON entry per test to tests\results.json. Same behaviour and output format
:: as tests/run_tests.sh.
::
:: usage: tests\run_tests.bat [options] [case ...]
::   case                 only run cases whose name contains this text (repeatable)
::   -b, --bin <path>     prism executable (default: %PRISM_BIN% or build\bin.exe)
::   -r, --results <file> results file to append to (default: tests\results.json)
::   -m, --machine <name> machine label stored with results (default: %PRISM_TEST_MACHINE% or computer name)
::   -n, --no-record      run and report, but don't write to the results file
::   -u, --update-truth   copy each render over its ground truth (doesn't record results)
::   -t, --timeout <s>    per-test timeout in seconds (default: 1800)
::   -h, --help           show this help
::
:: cmd can't do floating point math or safe JSON escaping, so the heavy lifting is
:: done by the PowerShell script embedded at the bottom of this file.
:: ------------------------------------------------------------------------------
setlocal

set "PRISM_TEST_SCRIPT=%~f0"
for %%I in ("%~dp0..") do set "PRISM_ROOT=%%~fI"
if not defined PRISM_BIN set "PRISM_BIN=build\bin.exe"
set "PRISM_RESULTS=%~dp0results.json"
if not defined PRISM_TEST_MACHINE set "PRISM_TEST_MACHINE=%COMPUTERNAME%"
set "PRISM_RECORD=1"
set "PRISM_UPDATE_TRUTH=0"
set "PRISM_TIMEOUT=1800"
set "PRISM_FILTERS="

:args
if "%~1"=="" goto args_done
if /i "%~1"=="-b"             ( set "PRISM_BIN=%~2"          & shift & shift & goto args )
if /i "%~1"=="--bin"          ( set "PRISM_BIN=%~2"          & shift & shift & goto args )
if /i "%~1"=="-r"             ( set "PRISM_RESULTS=%~f2"     & shift & shift & goto args )
if /i "%~1"=="--results"      ( set "PRISM_RESULTS=%~f2"     & shift & shift & goto args )
if /i "%~1"=="-m"             ( set "PRISM_TEST_MACHINE=%~2" & shift & shift & goto args )
if /i "%~1"=="--machine"      ( set "PRISM_TEST_MACHINE=%~2" & shift & shift & goto args )
if /i "%~1"=="-t"             ( set "PRISM_TIMEOUT=%~2"      & shift & shift & goto args )
if /i "%~1"=="--timeout"      ( set "PRISM_TIMEOUT=%~2"      & shift & shift & goto args )
if /i "%~1"=="-n"             ( set "PRISM_RECORD=0"         & shift & goto args )
if /i "%~1"=="--no-record"    ( set "PRISM_RECORD=0"         & shift & goto args )
if /i "%~1"=="-u"             ( set "PRISM_UPDATE_TRUTH=1"   & set "PRISM_RECORD=0" & shift & goto args )
if /i "%~1"=="--update-truth" ( set "PRISM_UPDATE_TRUTH=1"   & set "PRISM_RECORD=0" & shift & goto args )
if /i "%~1"=="-h"             goto help
if /i "%~1"=="--help"         goto help
set "PRISM_ARG=%~1"
if "%PRISM_ARG:~0,1%"=="-" (
    echo Unknown option "%~1"
    goto help_error
)
set "PRISM_FILTERS=%PRISM_FILTERS%|%~1"
shift
goto args

:args_done
where powershell >nul 2>nul
if errorlevel 1 (
    echo PowerShell is required to run the prism test suite
    exit /b 2
)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$s=[IO.File]::ReadAllText($env:PRISM_TEST_SCRIPT); $m='#__PRISM'+'_POWERSHELL__'; Invoke-Expression $s.Substring($s.IndexOf($m))"
exit /b %ERRORLEVEL%

:help
powershell -NoProfile -Command "Get-Content -LiteralPath $env:PRISM_TEST_SCRIPT | Where-Object { $_.StartsWith('::   ') -or $_.StartsWith(':: usage') } | ForEach-Object { $_.Substring(3) }"
exit /b 0

:help_error
call :help
exit /b 2

#__PRISM_POWERSHELL__
# ------------------------------------------------------------------------------
# Everything below is PowerShell (5.1 compatible). It reads its configuration from
# the PRISM_* environment variables set above.
# ------------------------------------------------------------------------------
$ErrorActionPreference = 'Continue'
$inv = [Globalization.CultureInfo]::InvariantCulture
$root = $env:PRISM_ROOT
Set-Location $root

function Out-Colour([string]$text, [string]$colour) {
    if ($colour) { Write-Host $text -ForegroundColor $colour } else { Write-Host $text }
}
function Resolve-RepoPath([string]$p) {
    if ([string]::IsNullOrEmpty($p)) { return '' }
    if ([IO.Path]::IsPathRooted($p)) { return $p }
    return [IO.Path]::GetFullPath((Join-Path $root $p))
}
function Get-RelPath([string]$p) {
    if ([string]::IsNullOrEmpty($p)) { return '' }
    $r = $p
    if ($p.StartsWith($root)) { $r = $p.Substring($root.Length).TrimStart('\', '/') }
    return $r.Replace('\', '/')
}
function ConvertTo-Bool($v) { return @('true', '1', 'yes', 'on') -contains ([string]$v).ToLower() }
function ConvertTo-Num($v) {
    $i = [long]0
    if ([long]::TryParse([string]$v, [Globalization.NumberStyles]::Integer, $inv, [ref]$i)) { return $i }
    $d = 0.0
    if ([double]::TryParse([string]$v, [Globalization.NumberStyles]::Float, $inv, [ref]$d)) { return $d }
    return $null
}
function Invoke-Git([string[]]$gitArgs) {
    try { $o = & git -C $root @gitArgs 2>$null; if ($LASTEXITCODE -eq 0 -and $o) { return ([string]($o | Select-Object -First 1)).Trim() } } catch { }
    return ''
}
function Get-JsonLine([string]$file, [string]$prefix) {
    if (-not (Test-Path -LiteralPath $file)) { return $null }
    $line = Get-Content -LiteralPath $file | Where-Object { $_.StartsWith($prefix + ' ') } | Select-Object -First 1
    if (-not $line) { return $null }
    try { return ($line.Substring($prefix.Length + 1) | ConvertFrom-Json) } catch { return $null }
}
$statusRank = @{ pass = 0; no_truth = 1; fail = 2; error = 3 }
function Get-Raised([string]$current, [string]$next) { if ($statusRank[$next] -gt $statusRank[$current]) { return $next } return $current }

function Add-Result([string]$file, [string]$entry) {
    $text = ''
    if (Test-Path -LiteralPath $file) { $text = [IO.File]::ReadAllText($file) }
    $nl = [string][char]10
    $close = $text.LastIndexOf(']')
    if ($text.Trim().Length -eq 0 -or $close -lt 0) {
        $out = '[' + $nl + '  ' + $entry + $nl + ']' + $nl
    } else {
        $head = $text.Substring(0, $close).TrimEnd()
        if ($head.EndsWith('[')) { $sep = '' } elseif ($head.EndsWith(',')) { $sep = '' } else { $sep = ',' }
        $out = $head + $sep + $nl + '  ' + $entry + $nl + ']' + $nl
    }
    $tmp = $file + '.tmp'
    [IO.File]::WriteAllText($tmp, $out, (New-Object Text.UTF8Encoding $false))
    Move-Item -LiteralPath $tmp -Destination $file -Force
}

# ----------------------------------------------------------- environment ----

$bin = Resolve-RepoPath $env:PRISM_BIN
if (-not (Test-Path -LiteralPath $bin)) {
    Out-Colour "prism executable not found at `"$($env:PRISM_BIN)`" - build it first with scripts\build.bat (or pass -b <path>)" Red
    exit 2
}
$runDir = Join-Path $root 'build/env'
if (-not (Test-Path -LiteralPath $runDir)) { $runDir = Split-Path -Parent $bin }
$record = $env:PRISM_RECORD -eq '1'
$updateTruth = $env:PRISM_UPDATE_TRUTH -eq '1'
$timeoutS = [int]$env:PRISM_TIMEOUT
$machine = $env:PRISM_TEST_MACHINE
$filters = @(([string]$env:PRISM_FILTERS).Split('|') | Where-Object { $_ -ne '' })

# git state
$commit = Invoke-Git @('rev-parse', 'HEAD'); if (-not $commit) { $commit = 'unknown' }
$commitShort = Invoke-Git @('rev-parse', '--short', 'HEAD'); if (-not $commitShort) { $commitShort = 'unknown' }
$branch = Invoke-Git @('rev-parse', '--abbrev-ref', 'HEAD'); if (-not $branch) { $branch = 'unknown' }
$commitDate = Invoke-Git @('show', '-s', '--format=%cI', 'HEAD')
$commitMsg = Invoke-Git @('show', '-s', '--format=%s', 'HEAD')
$remote = Invoke-Git @('remote', 'get-url', 'origin')
$dirty = $false
try { $st = & git -C $root status --porcelain --untracked-files=no 2>$null; if ($LASTEXITCODE -eq 0 -and $st) { $dirty = $true } } catch { }

# hardware / os
$cpu = ''; $cores = $null; $ramGb = $null; $osName = ''; $osVersion = ''; $gpuFallback = ''; $driverByGpu = @{}
try {
    $p = Get-CimInstance Win32_Processor -ErrorAction Stop | Select-Object -First 1
    $cpu = ([string]$p.Name).Trim()
    $cores = [int](Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
    $ramGb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
    $os = Get-CimInstance Win32_OperatingSystem
    $osName = ([string]$os.Caption).Trim()
    $osVersion = [string]$os.Version
    foreach ($vc in (Get-CimInstance Win32_VideoController)) {
        if (-not $gpuFallback) { $gpuFallback = [string]$vc.Name }
        $driverByGpu[[string]$vc.Name] = [string]$vc.DriverVersion
    }
} catch {
    # not on windows (or CIM unavailable) - fall back to whatever .NET knows
    $osName = [Environment]::OSVersion.VersionString
    $osVersion = [string][Environment]::OSVersion.Version
    $cores = [Environment]::ProcessorCount
}

$sessionStart = [DateTime]::UtcNow
$session = $sessionStart.ToString('yyyyMMddTHHmmssZ', $inv) + '-' + ($machine -replace '[^A-Za-z0-9_.-]', '_')
$outDir = Join-Path $root ('tests/output/' + $session)
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$results = $env:PRISM_RESULTS

Out-Colour 'prism test suite' Cyan
Write-Host "  session : $session"
$dirtyText = ''; if ($dirty) { $dirtyText = ' + uncommitted changes' }
Write-Host "  commit  : $commitShort ($branch)$dirtyText"
Write-Host "  machine : $machine - $osName, $cpu"
Write-Host "  output  : $(Get-RelPath $outDir)"
if (-not $record) { Out-Colour '  results will not be recorded' Yellow }
Write-Host ''

# ------------------------------------------------------------------ cases ----

$cases = @(Get-ChildItem -Path (Join-Path $root 'tests/cases') -Filter '*.test' -File -ErrorAction SilentlyContinue | Sort-Object Name)
if ($cases.Count -eq 0) { Out-Colour 'No test cases found in tests/cases/' Yellow; exit 0 }

$passed = 0; $failed = 0; $errored = 0; $skipped = 0
$summary = New-Object System.Collections.Generic.List[string]
$gpuSeen = ''

foreach ($case in $cases) {
    $t = [ordered]@{
        name = [IO.Path]::GetFileNameWithoutExtension($case.Name); description = ''; scene = ''; truth = ''
        samples = '64'
        min_ssim = ''; min_psnr = ''; max_rmse = ''; max_render_time = ''; enabled = 'true'; tags = ''
    }
    $warnings = New-Object System.Collections.Generic.List[string]
    foreach ($raw in [IO.File]::ReadAllLines($case.FullName)) {
        $line = $raw
        $hash = $line.IndexOf('#'); if ($hash -ge 0) { $line = $line.Substring(0, $hash) }
        $eq = $line.IndexOf('='); if ($eq -lt 0) { continue }
        $key = $line.Substring(0, $eq).Trim(); $val = $line.Substring($eq + 1).Trim()
        if ($key -eq 'ground_truth') { $key = 'truth' }
        if (@('width', 'height', 'direct_lighting', 'direct_only') -contains $key) { $warnings.Add("`"$key`" is ignored - render settings come from the scene file"); continue }
        if ($t.Contains($key)) { $t[$key] = $val } else { $warnings.Add("unknown key `"$key`"") }
    }

    if ($filters.Count -gt 0) {
        $match = $false
        foreach ($f in $filters) { if ($t.name.Contains($f)) { $match = $true } }
        if (-not $match) { continue }
    }
    if (-not (ConvertTo-Bool $t.enabled)) { Out-Colour "- $($t.name) (disabled)" DarkGray; $skipped++; continue }

    Write-Host "> $($t.name) " -ForegroundColor Cyan -NoNewline
    Out-Colour "$($t.samples) spp" DarkGray
    foreach ($w in $warnings) { Out-Colour "  warning: $w (tests/cases/$($case.Name))" Yellow }

    $safe = $t.name -replace '[^A-Za-z0-9_.-]', '_'
    $renderOut = Join-Path $outDir "$safe.png"
    $diffOut = Join-Path $outDir "$safe.diff.png"
    $logOut = Join-Path $outDir "$safe.log"
    $status = 'pass'; $reasons = New-Object System.Collections.Generic.List[string]; $exitCode = 0
    $renderJson = $null; $settingsJson = $null; $compareJson = $null; $renderS = $null; $wallS = $null; $res = ''

    $sceneAbs = Resolve-RepoPath $t.scene
    $truthAbs = Resolve-RepoPath $t.truth

    if (-not $t.scene) {
        $status = 'error'; $reasons.Add('no scene specified')
    } elseif (-not (Test-Path -LiteralPath $sceneAbs)) {
        $status = 'error'; $reasons.Add("scene file not found: $($t.scene)")
    } else {
        $argLine = "render `"$sceneAbs`" `"$renderOut`" $($t.samples)"
        $errLog = "$logOut.stderr"
        $sw = [Diagnostics.Stopwatch]::StartNew()
        $proc = Start-Process -FilePath $bin -ArgumentList $argLine -WorkingDirectory $runDir -NoNewWindow -PassThru `
            -RedirectStandardOutput $logOut -RedirectStandardError $errLog
        $null = $proc.Handle # caches the handle so ExitCode is available after exit
        if (-not $proc.WaitForExit($timeoutS * 1000)) {
            try { $proc.Kill() } catch { }
            $exitCode = 124
        } else {
            $proc.WaitForExit()
            $exitCode = $proc.ExitCode
        }
        $sw.Stop()
        $wallS = [math]::Round($sw.Elapsed.TotalSeconds, 3)
        if (Test-Path -LiteralPath $errLog) { Get-Content -LiteralPath $errLog | Add-Content -LiteralPath $logOut; Remove-Item -LiteralPath $errLog }
        $renderJson = Get-JsonLine $logOut 'PRISM_RENDER_JSON'
        if ($renderJson) { $renderS = $renderJson.render_seconds; if ($renderJson.gpu) { $gpuSeen = $renderJson.gpu } }
        $settingsJson = Get-JsonLine $logOut 'PRISM_SETTINGS_JSON'
        if ($settingsJson) { $res = "$($settingsJson.width)x$($settingsJson.height)" }

        if ($exitCode -eq 124) { $status = 'error'; $reasons.Add("timed out after ${timeoutS}s") }
        elseif ($exitCode -ne 0) { $status = 'error'; $reasons.Add("prism exited with code $exitCode") }
        elseif (-not (Test-Path -LiteralPath $renderOut)) { $status = 'error'; $reasons.Add('prism did not write a render') }
        if ($status -eq 'error' -and (Test-Path -LiteralPath $logOut)) {
            # surface the actual cause: executor errors, failed asserts, fatals
            $causes = Get-Content -LiteralPath $logOut | Where-Object { $_ -match '^PRISM_EXECUTOR_ERROR |\[(FAIL|FATAL|ERROR)\]' } | Select-Object -First 3
            foreach ($c in $causes) { $reasons.Add((($c -replace "$([char]27)\[[0-9;]*m", '') -replace '^PRISM_EXECUTOR_ERROR ', '').Trim()) }
            if (Test-Path -LiteralPath $renderOut) { $reasons.Add('a render was written before the failure - metrics still recorded') }
        }
    }

    if ($status -ne 'error' -and $updateTruth) {
        if (-not $t.truth) {
            Out-Colour '  no truth path set - nothing to update' Yellow
        } else {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $truthAbs) | Out-Null
            Copy-Item -LiteralPath $renderOut -Destination $truthAbs -Force
            Out-Colour "  ground truth updated -> $($t.truth)" Green
        }
    } elseif (-not $updateTruth -and (Test-Path -LiteralPath $renderOut)) {
        if (-not $t.truth) {
            $status = Get-Raised $status 'no_truth'; $reasons.Add('no ground truth specified')
        } elseif (-not (Test-Path -LiteralPath $truthAbs)) {
            $status = Get-Raised $status 'no_truth'; $reasons.Add("ground truth not found: $($t.truth) (create it with -u)")
        } else {
            $cmpOut = & $bin compare $renderOut $truthAbs $diffOut 2>&1
            $cmpCode = $LASTEXITCODE
            $cmpOut | ForEach-Object { [string]$_ } | Add-Content -LiteralPath $logOut
            $compareJson = Get-JsonLine $logOut 'PRISM_COMPARE_JSON'
            if ($cmpCode -ne 0 -or -not $compareJson -or $compareJson.error) {
                $status = Get-Raised $status 'error'; $reasons.Add("image comparison failed (code $cmpCode) - see log"); $compareJson = $null
            } else {
                $v = ConvertTo-Num $t.min_ssim; if ($null -ne $v -and $compareJson.ssim -lt $v) { $status = Get-Raised $status 'fail'; $reasons.Add("ssim $($compareJson.ssim) < $v") }
                $v = ConvertTo-Num $t.min_psnr; if ($null -ne $v -and $compareJson.psnr -lt $v) { $status = Get-Raised $status 'fail'; $reasons.Add("psnr $($compareJson.psnr) < $v") }
                $v = ConvertTo-Num $t.max_rmse; if ($null -ne $v -and $compareJson.rmse -gt $v) { $status = Get-Raised $status 'fail'; $reasons.Add("rmse $($compareJson.rmse) > $v") }
            }
        }
        $v = ConvertTo-Num $t.max_render_time
        if ($null -ne $v -and $null -ne $renderS -and $renderS -gt $v) { $status = Get-Raised $status 'fail'; $reasons.Add("render time ${renderS}s > ${v}s") }
    }

    switch ($status) {
        'pass'  { $passed++;  $colour = 'Green' }
        'fail'  { $failed++;  $colour = 'Red' }
        'error' { $errored++; $colour = 'Red' }
        default { $skipped++; $colour = 'Yellow' }
    }
    $detail = ''
    if ($null -ne $renderS) { $detail = "render ${renderS}s"; if ($res) { $detail = "$res, $detail" } }
    if ($compareJson) { $detail += ", ssim $($compareJson.ssim), psnr $($compareJson.psnr) dB" }
    Write-Host "  $status " -ForegroundColor $colour -NoNewline
    Out-Colour $detail DarkGray
    foreach ($r in $reasons) { Write-Host "    - $r" }
    if ($status -eq 'error' -and (Test-Path -LiteralPath $logOut)) {
        Out-Colour "    last lines of $(Get-RelPath $logOut):" DarkGray
        Get-Content -LiteralPath $logOut -Tail 8 | ForEach-Object { Write-Host ('      | ' + ($_ -replace "[$([char]8)]", '')) }
    }
    $summary.Add(('{0,-8} {1}' -f $status, $t.name))

    if (-not $record) { continue }

    $gpuName = ''
    if ($renderJson -and $renderJson.gpu) { $gpuName = $renderJson.gpu } elseif ($gpuSeen) { $gpuName = $gpuSeen } else { $gpuName = $gpuFallback }
    if (-not $gpuName) { $gpuName = 'unknown' }
    $driver = ''
    foreach ($k in $driverByGpu.Keys) { if ($gpuName -like "*$k*" -or $k -like "*$gpuName*") { $driver = $driverByGpu[$k] } }

    $outputRel = ''; if (Test-Path -LiteralPath $renderOut) { $outputRel = Get-RelPath $renderOut }
    $diffRel = ''; if (Test-Path -LiteralPath $diffOut) { $diffRel = Get-RelPath $diffOut }
    $entry = [ordered]@{
        session       = $session
        timestamp     = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ', $inv)
        session_start = $sessionStart.ToString('yyyy-MM-ddTHH:mm:ssZ', $inv)
        test          = $t.name
        description   = $t.description
        tags          = $t.tags
        status        = $status
        reasons       = [string[]]$reasons.ToArray()
        exit_code     = $exitCode
        commit        = [ordered]@{ hash = $commit; short = $commitShort; branch = $branch; date = $commitDate; message = $commitMsg; dirty = $dirty; remote = $remote }
        device        = [ordered]@{ gpu = $gpuName; driver = $driver; cpu = $cpu; cores = $cores; ram_gb = $ramGb; os = $osName; os_version = $osVersion; machine = $machine }
        scene         = $t.scene.Replace('\', '/')
        ground_truth  = $t.truth.Replace('\', '/')
        output        = $outputRel
        diff          = $diffRel
        resolution    = $res
        settings      = $(if ($settingsJson) { $settingsJson } else { [ordered]@{ samples = (ConvertTo-Num $t.samples) } })
        thresholds    = [ordered]@{ min_ssim = (ConvertTo-Num $t.min_ssim); min_psnr = (ConvertTo-Num $t.min_psnr); max_rmse = (ConvertTo-Num $t.max_rmse); max_render_time = (ConvertTo-Num $t.max_render_time) }
        render_time   = $renderS
        wall_time     = $wallS
        metrics       = $compareJson
    }
    $json = ConvertTo-Json -InputObject $entry -Compress -Depth 6
    # keep results.json free of PowerShell's \u-escaping of < > ' & so diffs match the bash runner
    foreach ($pair in @(@('3c', '<'), @('3e', '>'), @('27', "'"), @('26', '&'))) { $json = $json.Replace(('\u00' + $pair[0]), $pair[1]) }
    try { Add-Result $results $json } catch { Out-Colour "  unable to write results to $results" Red }
}

# ---------------------------------------------------------------- summary ----

Write-Host ''
Write-Host 'summary ' -ForegroundColor Cyan -NoNewline
Out-Colour "($session)" DarkGray
foreach ($s in $summary) { Write-Host "  $s" }
Write-Host ''
Write-Host "  $passed passed" -ForegroundColor Green -NoNewline; Write-Host ', ' -NoNewline
Write-Host "$failed failed" -ForegroundColor Red -NoNewline; Write-Host ', ' -NoNewline
Write-Host "$errored errored" -ForegroundColor Red -NoNewline; Write-Host ', ' -NoNewline
Write-Host "$skipped skipped / without ground truth" -ForegroundColor Yellow
if ($record) { Write-Host "  results appended to $(Get-RelPath $results)" }
if ($failed -eq 0 -and $errored -eq 0) { exit 0 } else { exit 1 }
