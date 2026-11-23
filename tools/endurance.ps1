# Test d'endurance : lance le frontend (démonstration accélérée, vidéos) et relève mémoire / CPU à intervalle régulier.
#   powershell -File tools\endurance.ps1 -DataDir testdata\endurance -Minutes 60
# Résultat : <DataDir>\endurance.csv et un résumé (début / fin, maximum, tendance).
param([string]$DataDir = 'testdata\endurance', [int]$Minutes = 60, [int]$IntervalSec = 30,
      [string]$Exe = "$PSScriptRoot\..\build\Release\RacecabFrontend.exe")
$ErrorActionPreference = 'Stop'
$DataDir = (Resolve-Path $DataDir).Path
$csv = Join-Path $DataDir 'endurance.csv'
$cores = [Environment]::ProcessorCount
$p = Start-Process -FilePath $Exe -ArgumentList '--data', "`"$DataDir`"", '--no-single-instance' -PassThru
Start-Sleep -Seconds 5
$rows = @()
$prevCpu = $p.TotalProcessorTime.TotalSeconds
$prevT = Get-Date
$end = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $end -and -not $p.HasExited) {
    Start-Sleep -Seconds $IntervalSec
    $p.Refresh()
    if ($p.HasExited) { break }
    $now = Get-Date
    $cpu = $p.TotalProcessorTime.TotalSeconds
    $pct = 100 * ($cpu - $prevCpu) / (($now - $prevT).TotalSeconds * $cores)
    $prevCpu = $cpu; $prevT = $now
    $rows += [pscustomobject]@{
        minute     = [math]::Round(($now - $p.StartTime).TotalMinutes, 1)
        cpu_pct    = [math]::Round($pct, 2)
        working_mb = [math]::Round($p.WorkingSet64 / 1MB, 1)
        private_mb = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
        handles    = $p.HandleCount
        threads    = $p.Threads.Count
    }
    $rows | Export-Csv -Path $csv -NoTypeInformation -Encoding UTF8
}
$crashed = $p.HasExited
if (-not $crashed) { $p.CloseMainWindow() | Out-Null; $p.WaitForExit(15000) | Out-Null }
$n = [math]::Max(1, [math]::Min(5, [int]($rows.Count / 4)))
$first = $rows | Select-Object -First $n; $last = $rows | Select-Object -Last $n
function Avg($set, $f) { [math]::Round(($set | Measure-Object -Property $f -Average).Average, 1) }
Write-Host "`nDurée : $($rows[-1].minute) min, $($rows.Count) relevés, plantage : $crashed, code de sortie : $($p.ExitCode)"
foreach ($f in 'working_mb', 'private_mb', 'handles', 'threads') {
    $max = ($rows | Measure-Object -Property $f -Maximum).Maximum
    Write-Host ("{0,-11} début {1,8}   fin {2,8}   max {3,8}" -f $f, (Avg $first $f), (Avg $last $f), $max)
}
Write-Host ("CPU moyen   {0} % (tous cœurs), max {1} %" -f (Avg $rows 'cpu_pct'), ($rows | Measure-Object -Property cpu_pct -Maximum).Maximum)
Write-Host "Relevés : $csv"
