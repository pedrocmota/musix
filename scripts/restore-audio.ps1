# Religa os serviços de áudio do Windows (rodar como administrador) e, se um
# endpoint for informado, mostra as permissões da chave dele no registro.
param([string]$EndpointId, [string]$LogPath)
$out = @()
try { Start-Service AudioEndpointBuilder -ErrorAction Stop; Start-Service Audiosrv -ErrorAction Stop; $out += 'servicos religados' } catch { $out += "falha ao religar: $($_.Exception.Message)" }
$out += (Get-Service Audiosrv, AudioEndpointBuilder | ForEach-Object { "$($_.Name)=$($_.Status)" })
if ($EndpointId) {
    $k = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\$EndpointId"
    try {
        $acl = Get-Acl $k
        $out += "owner: $($acl.Owner)"
        $out += ($acl.Access | ForEach-Object { "  $($_.IdentityReference) | $($_.RegistryRights) | inherited=$($_.IsInherited) | $($_.InheritanceFlags)/$($_.PropagationFlags)" })
        $out += "subchaves: " + ((Get-ChildItem $k | ForEach-Object { $_.PSChildName }) -join ', ')
        try { New-Item "$k\FxProperties" -ErrorAction Stop | Out-Null; $out += 'create FxProperties: ok'; Remove-Item "$k\FxProperties" -ErrorAction SilentlyContinue } catch { $out += "create FxProperties: $($_.Exception.Message)" }
    } catch { $out += "endpoint: $($_.Exception.Message)" }
}
if ($LogPath) { $out | Out-File -FilePath $LogPath -Encoding utf8 } else { $out }
