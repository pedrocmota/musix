//! Protocolo de configuração com o APO: `%ProgramData%\Musix\musix.cfg`
//! (gravado pelo app, lido pelo APO a cada 150 ms) e `status.txt` (gravado
//! pelo APO ao travar/destravar o formato).

use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Config {
    pub enabled: bool,
    pub voice_cancel: bool,
    pub pitch: i32,
    pub order: i32,
}

impl Default for Config {
    fn default() -> Self {
        Config { enabled: true, voice_cancel: false, pitch: 0, order: 0 }
    }
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Status {
    pub present: bool,
    pub locked: bool,
    pub rate: i32,
    pub channels: i32,
    pub pid: i32,
    pub time: String,
    pub config_writable: bool,
}

/// `%ProgramData%\Musix` (ou `MUSIX_DIR`, só para testes).
pub fn data_dir() -> PathBuf {
    if let Ok(d) = std::env::var("MUSIX_DIR") {
        if !d.is_empty() {
            return PathBuf::from(d);
        }
    }
    let pd = std::env::var("ProgramData").unwrap_or_else(|_| "C:\\ProgramData".into());
    PathBuf::from(pd).join("Musix")
}

/// `%ProgramFiles%\Musix`: onde a DLL do APO fica (o audiodg.exe precisa ler).
pub fn program_dir() -> PathBuf {
    let pf = std::env::var("ProgramFiles").unwrap_or_else(|_| "C:\\Program Files".into());
    PathBuf::from(pf).join("Musix")
}

pub fn cfg_path() -> PathBuf {
    data_dir().join("musix.cfg")
}

pub fn status_path() -> PathBuf {
    data_dir().join("status.txt")
}

pub fn log_path() -> PathBuf {
    data_dir().join("install.log")
}

fn parse_kv(text: &str) -> Vec<(String, String)> {
    text.lines()
        .filter_map(|l| {
            let (k, v) = l.split_once('=')?;
            Some((k.trim().to_ascii_lowercase(), v.trim().to_string()))
        })
        .collect()
}

fn get_int(kv: &[(String, String)], key: &str, default: i32) -> i32 {
    kv.iter().find(|(k, _)| k == key).and_then(|(_, v)| v.parse().ok()).unwrap_or(default)
}

fn get_str(kv: &[(String, String)], key: &str) -> String {
    kv.iter().find(|(k, _)| k == key).map(|(_, v)| v.clone()).unwrap_or_default()
}

pub fn read_config() -> Config {
    let d = Config::default();
    match std::fs::read_to_string(cfg_path()) {
        Ok(text) => {
            let kv = parse_kv(&text);
            Config {
                enabled: get_int(&kv, "enabled", 1) != 0,
                voice_cancel: get_int(&kv, "voicecancel", 0) != 0,
                pitch: get_int(&kv, "pitch", 0).clamp(-4, 4),
                order: if get_int(&kv, "order", 0) != 0 { 1 } else { 0 },
            }
        }
        Err(_) => d,
    }
}

pub fn config_text(c: &Config) -> String {
    format!(
        "enabled={}\r\nvoicecancel={}\r\npitch={}\r\norder={}\r\n",
        c.enabled as i32,
        c.voice_cancel as i32,
        c.pitch.clamp(-4, 4),
        c.order.clamp(0, 1)
    )
}

pub fn write_config(c: &Config) -> Result<(), String> {
    let dir = data_dir();
    if !dir.is_dir() {
        return Err("err.config_dir".into());
    }
    std::fs::write(cfg_path(), config_text(c)).map_err(|e| format!("err.config_write|{}: {e}", cfg_path().display()))
}

pub fn read_status() -> Status {
    let dir = data_dir();
    let config_writable = dir.is_dir()
        && std::fs::OpenOptions::new().append(true).create(true).open(cfg_path()).is_ok();
    match std::fs::read_to_string(status_path()) {
        Ok(text) => {
            let kv = parse_kv(&text);
            Status {
                present: true,
                locked: get_int(&kv, "locked", 0) != 0,
                rate: get_int(&kv, "rate", 0),
                channels: get_int(&kv, "channels", 0),
                pid: get_int(&kv, "pid", 0),
                time: get_str(&kv, "time"),
                config_writable,
            }
        }
        Err(_) => Status { present: false, locked: false, rate: 0, channels: 0, pid: 0, time: String::new(), config_writable },
    }
}
