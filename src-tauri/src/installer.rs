//! Lógica executada pelo processo elevado (`Musix.exe --elevated ...`):
//! instala/desinstala o APO num endpoint, compatível com o `install.ps1`
//! do projeto original (mesmas chaves, mesmo formato de backup).

use serde_json::{json, Map, Value};
use std::fs::OpenOptions;
use std::io::Write;
use std::path::{Path, PathBuf};
use winreg::enums::{HKEY_LOCAL_MACHINE, KEY_ALL_ACCESS, KEY_READ, REG_DWORD, REG_MULTI_SZ, REG_SZ};
use winreg::{RegKey, RegValue};

use crate::config;
use crate::i18n::{t, tf, Lang};
use crate::sec;
use crate::services;
use crate::state::{self, CLSID, KEY_DISABLEFX};

pub struct Log {
    file: Option<std::fs::File>,
    lang: Lang,
}

impl Log {
    fn new(lang: Lang) -> Log {
        let _ = std::fs::create_dir_all(config::data_dir());
        let file = OpenOptions::new().create(true).write(true).truncate(true).open(config::log_path()).ok();
        Log { file, lang }
    }
    pub fn line(&mut self, s: impl AsRef<str>) {
        let s = s.as_ref();
        eprintln!("{s}");
        if let Some(f) = self.file.as_mut() {
            let _ = writeln!(f, "{s}");
            let _ = f.flush();
        }
    }
    fn msg(&mut self, key: &str, args: &[&str]) {
        let s = tf(self.lang, key, args);
        self.line(s);
    }
}

/// `--elevated <install|uninstall> <guid> [--after] [--dll <caminho>] [--lang pt|en]`
pub fn elevated_main(args: &[String]) -> i32 {
    let mut lang = Lang::Pt;
    let mut dll: Option<PathBuf> = None;
    let mut after = false;
    let mut i = 2;
    while i < args.len() {
        match args[i].as_str() {
            "--dll" if i + 1 < args.len() => { dll = Some(PathBuf::from(&args[i + 1])); i += 1; }
            "--lang" if i + 1 < args.len() => { lang = Lang::parse(&args[i + 1]); i += 1; }
            "--after" => after = true,
            _ => {}
        }
        i += 1;
    }
    let mut log = Log::new(lang);
    let action = args.first().cloned().unwrap_or_default();
    let guid = args.get(1).cloned().unwrap_or_default();
    if !state::valid_guid(&guid) {
        let m = tf(lang, "invalid_endpoint", &[&guid]);
        log.msg("error", &[&m]);
        return 2;
    }
    let r = match action.as_str() {
        "install" => match dll {
            Some(d) => install(&guid, &d, after, &mut log),
            None => Err(t(lang, "missing_dll_arg")),
        },
        "uninstall" => uninstall(&guid, &mut log),
        _ => Err(tf(lang, "unknown_action", &[&action])),
    };
    match r {
        Ok(()) => { log.msg("ok", &[]); 0 }
        Err(e) => { log.msg("error", &[&e]); 1 }
    }
}

// ---------------------------------------------------------------- registro

fn open_fx(lang: Lang, guid: &str, write: bool) -> Result<RegKey, String> {
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    hklm.open_subkey_with_flags(state::fx_subkey(guid), if write { KEY_ALL_ACCESS } else { KEY_READ })
        .map_err(|e| tf(lang, "open_fx", &[&e.to_string()]))
}

/// {"<nome>": {"Kind": "String|MultiString|DWord", "Value": ...}}
fn snapshot(fx: &RegKey) -> Map<String, Value> {
    let mut m = Map::new();
    for name in state::all_keys() {
        if let Ok(rv) = fx.get_raw_value(&name) {
            let (kind, value) = match rv.vtype {
                REG_SZ => ("String", json!(decode_sz(&rv))),
                REG_MULTI_SZ => ("MultiString", json!(decode_multi(&rv))),
                REG_DWORD => ("DWord", json!(decode_dword(&rv))),
                _ => continue,
            };
            m.insert(name, json!({ "Kind": kind, "Value": value }));
        }
    }
    m
}

fn decode_sz(rv: &RegValue) -> String {
    let u: Vec<u16> = rv.bytes.chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect();
    let end = u.iter().position(|&c| c == 0).unwrap_or(u.len());
    String::from_utf16_lossy(&u[..end])
}

fn decode_multi(rv: &RegValue) -> Vec<String> {
    let u: Vec<u16> = rv.bytes.chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect();
    u.split(|&c| c == 0).filter(|s| !s.is_empty()).map(String::from_utf16_lossy).collect()
}

fn decode_dword(rv: &RegValue) -> u32 {
    let mut b = [0u8; 4];
    b.copy_from_slice(&rv.bytes[..4.min(rv.bytes.len())]);
    u32::from_le_bytes(b)
}

fn value_strings(v: &Value) -> Vec<String> {
    match v {
        Value::Array(a) => a.iter().filter_map(|x| x.as_str().map(String::from)).filter(|s| !s.is_empty()).collect(),
        Value::String(s) if !s.is_empty() => vec![s.clone()],
        _ => vec![],
    }
}

fn restore_value(lang: Lang, fx: &RegKey, name: &str, entry: &Value) -> Result<(), String> {
    let kind = entry.get("Kind").and_then(|k| k.as_str()).unwrap_or("String");
    let value = entry.get("Value").cloned().unwrap_or(Value::Null);
    let r = match kind {
        "MultiString" => fx.set_value(name, &value_strings(&value)),
        "DWord" => fx.set_value(name, &(value.as_u64().unwrap_or(0) as u32)),
        _ => fx.set_value(name, &value.as_str().unwrap_or("").to_string()),
    };
    r.map_err(|e| tf(lang, "restore_value", &[name, &e.to_string()]))
}

fn read_backup(lang: Lang, path: &Path) -> Result<Map<String, Value>, String> {
    let text = std::fs::read_to_string(path).map_err(|e| tf(lang, "read_backup", &[&e.to_string()]))?;
    let text = text.trim_start_matches('\u{feff}'); // o PowerShell grava BOM
    let v: Value = serde_json::from_str(text).map_err(|e| tf(lang, "bad_backup", &[&e.to_string()]))?;
    match v {
        Value::Object(m) => Ok(m),
        _ => Err(tf(lang, "bad_backup", &["json"])),
    }
}

/// Lista original de CLSIDs de um slot: a composta, senão a legada. Backups de
/// versões antigas do instalador não têm todas as chaves; nesse caso cai no
/// valor atual do registro (que o instalador nunca alterou).
fn orig_list(orig: &Map<String, Value>, fx: &RegKey, composite: &str, legacy: &str) -> Vec<String> {
    if let Some(e) = orig.get(composite) {
        return value_strings(e.get("Value").unwrap_or(&Value::Null));
    }
    if let Some(e) = orig.get(legacy) {
        return value_strings(e.get("Value").unwrap_or(&Value::Null));
    }
    if let Ok(rv) = fx.get_raw_value(composite) {
        if rv.vtype == REG_MULTI_SZ {
            return decode_multi(&rv).into_iter().filter(|c| !c.eq_ignore_ascii_case(CLSID)).collect();
        }
    }
    if let Ok(rv) = fx.get_raw_value(legacy) {
        if rv.vtype == REG_SZ {
            let s = decode_sz(&rv);
            if !s.is_empty() && !s.eq_ignore_ascii_case(CLSID) {
                return vec![s];
            }
        }
    }
    vec![]
}

/// Chaves que o instalador pode ter criado (e que, portanto, a desinstalação
/// pode remover quando não constam do backup). As legadas 5 e 7 e o
/// Disable_SysFx nunca são criados por nós.
fn keys_we_create() -> Vec<String> {
    vec![state::key_mfx(), state::key_csfx(), state::key_cmfx(), state::key_cefx()]
}

fn set_list(lang: Lang, fx: &RegKey, name: &str, list: &[String]) -> Result<(), String> {
    fx.set_value(name, &list.to_vec()).map_err(|e| tf(lang, "write_value", &[name, &e.to_string()]))
}

fn delete_if_present(lang: Lang, fx: &RegKey, name: &str) -> Result<bool, String> {
    if fx.get_raw_value(name).is_ok() {
        fx.delete_value(name).map_err(|e| tf(lang, "delete_value", &[name, &e.to_string()]))?;
        return Ok(true);
    }
    Ok(false)
}

/// Religa o áudio ao sair do escopo, mesmo em erro (o `?` não pode deixar o
/// Windows mudo). Chamar start de novo com o serviço rodando é inócuo.
struct AudioStopped;
impl Drop for AudioStopped {
    fn drop(&mut self) {
        let _ = services::start_audio();
    }
}

/// Deixa a chave do endpoint e a FxProperties nas mãos dos Administradores e
/// devolve a FxProperties aberta para escrita. Endpoints Bluetooth já trazem
/// uma FxProperties com ACL restrita: tem que tomar posse dela antes de
/// abrir com acesso total.
fn prepare_fx_key(lang: Lang, guid: &str) -> Result<RegKey, String> {
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    let ep_sub = state::endpoint_subkey(guid);
    let fx_sub = state::fx_subkey(guid);
    sec::own_registry_key(&ep_sub)?;
    let ep = hklm.open_subkey_with_flags(&ep_sub, KEY_ALL_ACCESS).map_err(|e| tf(lang, "open_endpoint", &[&e.to_string()]))?;
    // cria se não existir; se existir, só abre para leitura (a posse vem a seguir)
    ep.create_subkey_with_flags("FxProperties", KEY_READ).map_err(|e| tf(lang, "create_fx", &[&e.to_string()]))?;
    sec::own_registry_key(&fx_sub)?;
    open_fx(lang, guid, true)
}

// ---------------------------------------------------------------- ações

fn install(guid: &str, dll_src: &Path, after: bool, log: &mut Log) -> Result<(), String> {
    let lang = log.lang;
    log.msg("installing", &[guid]);
    if !dll_src.is_file() {
        return Err(tf(lang, "dll_missing", &[&dll_src.display().to_string()]));
    }
    RegKey::predef(HKEY_LOCAL_MACHINE)
        .open_subkey(state::endpoint_subkey(guid))
        .map_err(|_| t(lang, "endpoint_missing"))?;

    sec::enable_privilege("SeTakeOwnershipPrivilege")?;
    sec::enable_privilege("SeRestorePrivilege")?;
    sec::enable_privilege("SeBackupPrivilege")?;

    // pastas
    let program_dir = config::program_dir();
    let data_dir = config::data_dir();
    std::fs::create_dir_all(&program_dir).map_err(|e| tf(lang, "create_dir", &[&program_dir.display().to_string(), &e.to_string()]))?;
    std::fs::create_dir_all(&data_dir).map_err(|e| tf(lang, "create_dir", &[&data_dir.display().to_string(), &e.to_string()]))?;
    sec::open_data_dir(&data_dir)?;
    log.msg("data_dir", &[&data_dir.display().to_string()]);
    if !config::cfg_path().exists() {
        std::fs::write(config::cfg_path(), config::config_text(&config::Config::default()))
            .map_err(|e| tf(lang, "write_cfg", &[&e.to_string()]))?;
    }

    // Tudo que pode falhar no registro vem ANTES de parar o áudio.
    let fx = prepare_fx_key(lang, guid)?;

    let backup = state::backup_path(guid);
    let orig = if backup.exists() {
        log.msg("backup_existing", &[&backup.display().to_string()]);
        read_backup(lang, &backup)?
    } else {
        let m = snapshot(&fx);
        std::fs::write(&backup, serde_json::to_string_pretty(&Value::Object(m.clone())).unwrap())
            .map_err(|e| tf(lang, "write_backup", &[&e.to_string()]))?;
        log.msg("backup_saved", &[&backup.display().to_string()]);
        m
    };

    // DLL: para o áudio para descarregar uma versão anterior, copia e registra.
    // A partir daqui o áudio é religado em qualquer saída da função.
    log.msg("stopping_audio", &[]);
    if let Err(e) = services::stop_audio() { log.msg("warning", &[&e]); }
    let _audio = AudioStopped;
    let dll_dst = program_dir.join("MusixAPO.dll");
    let mut last = Ok(());
    for _ in 0..10 {
        last = std::fs::copy(dll_src, &dll_dst).map(|_| ());
        if last.is_ok() { break; }
        std::thread::sleep(std::time::Duration::from_millis(300));
    }
    last.map_err(|e| tf(lang, "copy_dll", &[&dll_dst.display().to_string(), &e.to_string()]))?;
    sec::dll_register(&dll_dst, true)?;
    log.msg("apo_registered", &[&dll_dst.display().to_string()]);

    // Dois modos, conforme o endpoint já usava:
    //  - lista composta (APOs de fabricante, ex.: Realtek): o Musix entra na
    //    lista, junto com o que já estava;
    //  - chave legada (APO genérico do Windows, ex.: HDMI/USB): o Musix ocupa
    //    o slot. Os APOs genéricos não carregam em lista composta (testado:
    //    [Musix, WM GFX] deixa o endpoint sem efeito nenhum). O backup
    //    devolve o genérico ao desinstalar.
    let key_mfx = state::key_mfx();
    let key_cmfx = state::key_cmfx();
    let composite_mode = orig.contains_key(&key_cmfx);
    if composite_mode {
        let others: Vec<String> = orig_list(&orig, &fx, &key_cmfx, &key_mfx)
            .into_iter()
            .filter(|c| !c.eq_ignore_ascii_case(CLSID))
            .collect();
        let mut mfx = Vec::new();
        if after { mfx.extend(others.clone()); mfx.push(CLSID.to_string()); } else { mfx.push(CLSID.to_string()); mfx.extend(others.clone()); }
        set_list(lang, &fx, &key_cmfx, &mfx)?;
        log.msg("mfx_composite", &[&mfx.join(" -> ")]);
        // chave legada do MFX volta ao original (instalações antigas a substituíam)
        match orig.get(&key_mfx) {
            Some(e) => restore_value(lang, &fx, &key_mfx, e)?,
            None => { delete_if_present(lang, &fx, &key_mfx)?; }
        }
    } else {
        let previous = orig_list(&orig, &fx, &key_cmfx, &key_mfx);
        fx.set_value(&key_mfx, &CLSID.to_string()).map_err(|e| tf(lang, "write_value", &[&key_mfx, &e.to_string()]))?;
        if previous.is_empty() {
            log.msg("mfx_legacy_empty", &[]);
        } else {
            log.msg("mfx_legacy_replace", &[&previous.join(", ")]);
        }
        // limpa listas compostas que uma instalação anterior do Musix tenha criado
        for comp in [state::key_csfx(), key_cmfx.clone(), state::key_cefx()] {
            if !orig.contains_key(&comp) {
                delete_if_present(lang, &fx, &comp)?;
            }
        }
    }
    if fx.get_raw_value(KEY_DISABLEFX).is_ok() {
        fx.set_value(KEY_DISABLEFX, &0u32).map_err(|e| tf(lang, "write_value", &[KEY_DISABLEFX, &e.to_string()]))?;
    }

    log.msg("starting_audio", &[]);
    services::start_audio()?;
    log.msg("installed", &[]);
    Ok(())
}

fn uninstall(guid: &str, log: &mut Log) -> Result<(), String> {
    let lang = log.lang;
    log.msg("removing", &[guid]);
    let backup = state::backup_path(guid);
    if !backup.exists() {
        return Err(tf(lang, "no_backup", &[&backup.display().to_string()]));
    }
    sec::enable_privilege("SeTakeOwnershipPrivilege")?;
    sec::enable_privilege("SeRestorePrivilege")?;
    let orig = read_backup(lang, &backup)?;
    let fx = prepare_fx_key(lang, guid)?;

    log.msg("stopping_audio", &[]);
    if let Err(e) = services::stop_audio() { log.msg("warning", &[&e]); }
    let _audio = AudioStopped;

    let removable = keys_we_create();
    for name in state::all_keys() {
        match orig.get(&name) {
            Some(e) => { restore_value(lang, &fx, &name, e)?; log.msg("restored", &[&name]); }
            None => {
                if removable.contains(&name) && delete_if_present(lang, &fx, &name)? {
                    log.msg("removed", &[&name]);
                }
            }
        }
    }
    std::fs::remove_file(&backup).map_err(|e| tf(lang, "delete_backup", &[&e.to_string()]))?;

    // sem mais endpoints instalados, desregistra o COM (a DLL fica na pasta)
    let remaining = std::fs::read_dir(config::data_dir())
        .map(|d| d.flatten().filter(|e| e.file_name().to_string_lossy().starts_with("backup-")).count())
        .unwrap_or(0);
    if remaining == 0 {
        let dll = config::program_dir().join("MusixAPO.dll");
        if dll.is_file() {
            match sec::dll_register(&dll, false) {
                Ok(()) => log.msg("com_unregistered", &[]),
                Err(e) => log.msg("warning", &[&e]),
            }
        }
    } else {
        log.msg("remaining", &[&remaining.to_string()]);
    }

    log.msg("starting_audio", &[]);
    services::start_audio()?;
    log.msg("removed_done", &[]);
    Ok(())
}
