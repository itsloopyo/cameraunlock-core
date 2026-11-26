#!/usr/bin/env pwsh
#Requires -Version 5.1
# The sampler Start-GameProcessSampler (IsolatedGameTest.psm1) runs: one CSV row every
# -IntervalSeconds for one process, until that process is gone or "<csv>.stop" appears.
#   time               local, ISO 8601
#   elapsedSeconds     since the sampler started
#   privateMB          private bytes
#   workingSetMB       working set
#   dedicatedVideoMB   the sum of \GPU Process Memory(pid_N_*)\Dedicated Usage, empty while
#                      Windows has no such counter for the process
#   cpuSeconds         processor time used since the process started, all threads
param(
    [Parameter(Mandatory)][int]$ProcessId,
    [Parameter(Mandatory)][string]$Path,
    [double]$IntervalSeconds = 2
)

$culture = [Globalization.CultureInfo]::InvariantCulture
$stop = "$Path.stop"
$megabyte = 1024 * 1024
$clock = [Diagnostics.Stopwatch]::StartNew()
$process = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
[IO.File]::WriteAllText($Path, "time,elapsedSeconds,privateMB,workingSetMB,dedicatedVideoMB,cpuSeconds`r`n")

while ($process -and -not $process.HasExited -and -not (Test-Path $stop)) {
    $started = $clock.Elapsed.TotalSeconds
    $process.Refresh()
    # Reading the counter takes about a second, which is why the wait below is what is left of the interval.
    $counter = Get-Counter "\GPU Process Memory(pid_${ProcessId}_*)\Dedicated Usage" -ErrorAction SilentlyContinue
    $video = $(if ($counter) { (($counter.CounterSamples | Measure-Object CookedValue -Sum).Sum / $megabyte).ToString('F1', $culture) } else { '' })
    # The process can go between the check above and these reads, and its numbers go with it.
    try {
        $row = @(
            (Get-Date).ToString('s'),
            $started.ToString('F1', $culture),
            ($process.PrivateMemorySize64 / $megabyte).ToString('F1', $culture),
            ($process.WorkingSet64 / $megabyte).ToString('F1', $culture),
            $video,
            $process.TotalProcessorTime.TotalSeconds.ToString('F2', $culture)
        ) -join ','
    } catch [InvalidOperationException] { break }
    [IO.File]::AppendAllText($Path, "$row`r`n")
    $left = $IntervalSeconds - ($clock.Elapsed.TotalSeconds - $started)
    while ($left -gt 0 -and -not $process.HasExited -and -not (Test-Path $stop)) {
        Start-Sleep -Milliseconds ([int][Math]::Min(200, $left * 1000))
        $left = $IntervalSeconds - ($clock.Elapsed.TotalSeconds - $started)
    }
}
Remove-Item $stop -ErrorAction SilentlyContinue
