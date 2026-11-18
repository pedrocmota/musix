#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Lang {
    Pt,
    En,
}

impl Lang {
    pub fn parse(s: &str) -> Lang {
        if s.eq_ignore_ascii_case("en") { Lang::En } else { Lang::Pt }
    }
}

fn pair(key: &str) -> (&'static str, &'static str) {
    match key {
        "installing" => ("Instalando Musix no endpoint {0}", "Installing Musix on endpoint {0}"),
        "endpoint_missing" => ("endpoint não encontrado no registro", "endpoint not found in the registry"),
        "data_dir" => ("Pasta de dados: {0} (Usuários e LOCAL SERVICE podem gravar)", "Data folder: {0} (Users and LOCAL SERVICE can write)"),
        "stopping_audio" => ("Parando o áudio do Windows...", "Stopping Windows audio..."),
        "warning" => ("aviso: {0}", "warning: {0}"),
        "apo_registered" => ("APO registrado: {0}", "APO registered: {0}"),
        "backup_existing" => ("Usando backup existente: {0}", "Using existing backup: {0}"),
        "backup_saved" => ("Backup salvo em {0}", "Backup saved to {0}"),
        "mfx_composite" => ("MFX (lista composta): {0}", "MFX (composite list): {0}"),
        "mfx_legacy_empty" => ("MFX (slot legado): Musix (o slot estava vazio)", "MFX (legacy slot): Musix (the slot was empty)"),
        "mfx_legacy_replace" => ("MFX (slot legado): Musix no lugar de {0} (o backup o restaura)", "MFX (legacy slot): Musix in place of {0} (the backup restores it)"),
        "starting_audio" => ("Religando o áudio do Windows...", "Restarting Windows audio..."),
        "installed" => ("Instalado. Toque um som nessa saída para o APO carregar.", "Installed. Play some audio on this output so the APO loads."),
        "removing" => ("Removendo Musix do endpoint {0}", "Removing Musix from endpoint {0}"),
        "no_backup" => ("não há backup para este endpoint ({0})", "there is no backup for this endpoint ({0})"),
        "restored" => ("restaurado {0}", "restored {0}"),
        "removed" => ("removido {0}", "removed {0}"),
        "com_unregistered" => ("APO desregistrado do COM (nenhum endpoint restante).", "APO unregistered from COM (no endpoints left)."),
        "remaining" => ("Ainda há {0} endpoint(s) com o Musix; o COM continua registrado.", "{0} endpoint(s) still have Musix; COM stays registered."),
        "removed_done" => ("Removido.", "Removed."),
        "ok" => ("OK", "OK"),
        "error" => ("ERRO: {0}", "ERROR: {0}"),
        "invalid_endpoint" => ("endpoint inválido: {0}", "invalid endpoint: {0}"),
        "unknown_action" => ("ação desconhecida: {0}", "unknown action: {0}"),
        "create_dir" => ("criar pasta {0}: {1}", "create folder {0}: {1}"),
        "write_cfg" => ("gravar musix.cfg: {0}", "write musix.cfg: {0}"),
        "write_dll" => ("gravar DLL em {0}: {1}", "write DLL to {0}: {1}"),
        "open_endpoint" => ("abrir chave do endpoint: {0}", "open endpoint key: {0}"),
        "create_fx" => ("criar FxProperties: {0}", "create FxProperties: {0}"),
        "open_fx" => ("abrir FxProperties: {0}", "open FxProperties: {0}"),
        "read_backup" => ("ler backup: {0}", "read backup: {0}"),
        "bad_backup" => ("backup inválido: {0}", "invalid backup: {0}"),
        "write_backup" => ("gravar backup: {0}", "write backup: {0}"),
        "write_value" => ("gravar {0}: {1}", "write {0}: {1}"),
        "delete_value" => ("remover {0}: {1}", "remove {0}: {1}"),
        "restore_value" => ("restaurar {0}: {1}", "restore {0}: {1}"),
        "delete_backup" => ("apagar backup: {0}", "delete backup: {0}"),
        _ => ("{0}", "{0}"),
    }
}

pub fn tf(lang: Lang, key: &str, args: &[&str]) -> String {
    let (pt, en) = pair(key);
    let mut s = (if lang == Lang::En { en } else { pt }).to_string();
    for (i, a) in args.iter().enumerate() {
        s = s.replace(&format!("{{{i}}}"), a);
    }
    s
}

pub fn t(lang: Lang, key: &str) -> String {
    tf(lang, key, &[])
}
