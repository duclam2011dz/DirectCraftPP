param([Parameter(Mandatory=$true)][string]$InputReport, [string]$Output="performance/gpu-report.json")
$ErrorActionPreference="Stop"; $content=Get-Content -LiteralPath $InputReport -Raw | ConvertFrom-Json; if($null -eq $content.gpuUtilizationPercent){throw "Input report has no gpuUtilizationPercent field."}
[ordered]@{version="1.1.2"; gpuUtilizationPercent=[double]$content.gpuUtilizationPercent; source=([string]$content.source); importedFrom=(Resolve-Path $InputReport).Path; importedAt=(Get-Date).ToUniversalTime().ToString("o")} | ConvertTo-Json | Set-Content -Encoding utf8 $Output
