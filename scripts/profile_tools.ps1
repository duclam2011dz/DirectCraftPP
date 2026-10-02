param([string]$Game="build/debug/Debug/DirectCraft.exe", [string]$Output="performance/capture", [int]$Seconds=10)
$ErrorActionPreference="Stop"
$parent=Split-Path -Parent $Output; if($parent){New-Item -ItemType Directory -Force $parent | Out-Null}
$wpr=Get-Command wpr -ErrorAction SilentlyContinue; $wpa=Get-Command wpa -ErrorAction SilentlyContinue; $pix=Get-Command pix -ErrorAction SilentlyContinue
[ordered]@{wpr=[bool]$wpr; wpa=[bool]$wpa; pix=[bool]$pix; gpuUtilizationPercent=$null; source="unavailable"} | ConvertTo-Json | Set-Content -Encoding utf8 "$Output.tools.json"
if(-not $wpr){Write-Warning "WPR is not installed; no ETL capture was created."; exit 0}
$etl="$Output.etl"; $oldPreference=$ErrorActionPreference; $ErrorActionPreference="Continue"; & wpr -cancel 2>$null | Out-Null; & wpr -start CPU -filemode | Out-Null; $startCode=$LASTEXITCODE; $ErrorActionPreference=$oldPreference
if($startCode -ne 0){Write-Warning "WPR could not start the CPU profile on this machine; no ETL capture was created."; exit 0}
try { $process=Start-Process -FilePath $Game -ArgumentList "--debug-tools" -PassThru; Start-Sleep -Seconds $Seconds; if(-not $process.HasExited){Stop-Process -Id $process.Id -Force} } finally { & wpr -stop $etl "DirectCraft++ v1.1.2 GPU/CPU capture" | Out-Null }
Write-Output "Capture written to $etl. Open it in WPA; GPU utilization must come from a supported export."
