use std::path::PathBuf;
use winreg::enums::HKEY_LOCAL_MACHINE;
use winreg::RegKey;

pub const CLSID: &str = "{6D7A2C5E-9B1F-4E0B-8A3D-1F2E3C4B5A69}";
pub const FX: &str = "{d04e05a6-594b-4fb6-a80d-01af5eed7d1d}";
pub const KEY_DISABLEFX: &str = "{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";

pub fn key_sfx() -> String { format!("{FX},5") }
pub fn key_mfx() -> String { format!("{FX},6") }
pub fn key_efx() -> String { format!("{FX},7") }
pub fn key_csfx() -> String { format!("{FX},13") }
pub fn key_cmfx() -> String { format!("{FX},14") }
pub fn key_cefx() -> String { format!("{FX},15") }

pub fn all_keys() -> Vec<String> {
    vec![key_sfx(), key_mfx(), key_efx(), key_csfx(), key_cmfx(), key_cefx(), KEY_DISABLEFX.to_string()]
}

pub fn endpoint_subkey(guid: &str) -> String {
    format!("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render\\{guid}")
}

pub fn fx_subkey(guid: &str) -> String {
    format!("{}\\FxProperties", endpoint_subkey(guid))
}

pub fn backup_path(guid: &str) -> PathBuf {
    crate::config::data_dir().join(format!("backup-{guid}.json"))
}

pub fn valid_guid(guid: &str) -> bool {
    guid.len() == 38
        && guid.starts_with('{')
        && guid.ends_with('}')
        && guid[1..37].chars().all(|c| c.is_ascii_hexdigit() || c == '-')
}

pub fn is_installed(guid: &str) -> bool {
    let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
    let Ok(fx) = hklm.open_subkey(fx_subkey(guid)) else { return false };
    if let Ok(list) = fx.get_value::<Vec<String>, _>(key_cmfx()) {
        if list.iter().any(|c| c.eq_ignore_ascii_case(CLSID)) {
            return true;
        }
    }
    if let Ok(s) = fx.get_value::<String, _>(key_mfx()) {
        if s.eq_ignore_ascii_case(CLSID) {
            return true;
        }
    }
    false
}
