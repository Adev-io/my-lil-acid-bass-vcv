# Closes the isolated Rack started by launch.ps1 and deletes its user dir. Refuses anything but an acidbass-rig-* dir
# under %TEMP%, and warns if a VCV login token got saved (a fresh dir can get logged in).
param([Parameter(Mandatory)][ValidatePattern('\\acidbass-rig-\d{8}-\d{6}$')][string]$Dir)
$ErrorActionPreference = 'Stop'
if (-not $Dir.StartsWith($env:TEMP)) { throw "Not under TEMP: $Dir" }
Get-Process Rack -ErrorAction SilentlyContinue | Where-Object {
    (Get-CimInstance Win32_Process -Filter "ProcessId=$($_.Id)").CommandLine -like "*$Dir*"
} | ForEach-Object { $_.CloseMainWindow() | Out-Null; $_ | Wait-Process -Timeout 30 }
$s = Get-Content "$Dir\settings.json" -Raw -ErrorAction SilentlyContinue | ConvertFrom-Json
if ($s.token) { Write-Warning "A VCV login token was saved in $Dir; deleting it with the dir." }
Remove-Item $Dir -Recurse -Force
"deleted $Dir"
