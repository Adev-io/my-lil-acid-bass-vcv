# What the isolated Rack holds right now, read from its own autosave (rewritten every ~15 s: wait 17 s after an action).
# Param ids: 24 = PATTERN, 27 = LENGTH (see AcidBass::ParamId).
param([Parameter(Mandatory)][string]$Dir)
$a = Get-Content "$Dir\autosave\patch.json" -Raw | ConvertFrom-Json
$m = $a.modules | Where-Object model -eq 'AcidBass'
if (@($m).Count -ne 1) { throw "Expected 1 AcidBass in $Dir autosave, found $(@($m).Count)" }
if (-not $m.data.patterns) { throw "No patterns in the AcidBass data" }
$len =($m.data.patterns[0..5] | ForEach-Object { $_.len }) -join '/'
"LENGTH={0} PATTERN={1} slots1-6 len={2} saved={3}" -f ($m.params | ? id -eq 27).value, ($m.params | ? id -eq 24).value, $len, (Get-Item "$Dir\autosave\patch.json").LastWriteTime.ToString('HH:mm:ss')
