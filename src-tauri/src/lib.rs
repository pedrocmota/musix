mod audio;
mod config;
mod elevate;
mod i18n;
mod installer;
mod sec;
mod services;
mod state;

use serde::Serialize;
use tauri::Manager;

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Output {
    pub id: String,
    pub guid: String,
    pub name: String,
    pub iface: String,
    pub is_default: bool,
    pub installed: bool,
    pub has_backup: bool,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ActionResult {
    pub ok: bool,
    pub code: i32,
    pub log: String,
}

fn outputs_now() -> Result<Vec<Output>, String> {
    let eps = audio::list_render_endpoints()?;
    Ok(eps
        .into_iter()
        .map(|e| Output {
            installed: state::is_installed(&e.guid),
            has_backup: state::backup_path(&e.guid).exists(),
            id: e.id,
            guid: e.guid,
            name: e.name,
            iface: e.iface,
            is_default: e.is_default,
        })
        .collect())
}

#[tauri::command]
async fn list_outputs() -> Result<Vec<Output>, String> {
    tauri::async_runtime::spawn_blocking(outputs_now).await.map_err(|e| e.to_string())?
}

#[tauri::command]
fn get_config() -> config::Config {
    config::read_config()
}

#[tauri::command]
fn set_config(config: config::Config) -> Result<(), String> {
    config::write_config(&config)
}

#[tauri::command]
fn get_status() -> config::Status {
    config::read_status()
}

#[tauri::command]
fn launch_args() -> Vec<String> {
    std::env::args().skip(1).collect()
}

async fn elevated_action(action: &'static str, guid: String, lang: Option<String>) -> Result<ActionResult, String> {
    if !state::valid_guid(&guid) {
        return Err("err.invalid_guid".into());
    }
    let lang = match lang.as_deref() { Some("en") => "en", _ => "pt" };
    let exe = std::env::current_exe().map_err(|e| format!("err.exe|{e}"))?;
    tauri::async_runtime::spawn_blocking(move || {
        let args = format!("--elevated {action} {guid} --lang {lang}");
        let code = elevate::run_elevated(&exe, &args)?;
        let log = std::fs::read_to_string(config::log_path()).unwrap_or_else(|_| "(sem registro da ação)".into());
        Ok(ActionResult { ok: code == 0, code, log })
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
async fn install_output(guid: String, lang: Option<String>) -> Result<ActionResult, String> {
    elevated_action("install", guid, lang).await
}

#[tauri::command]
async fn uninstall_output(guid: String, lang: Option<String>) -> Result<ActionResult, String> {
    elevated_action("uninstall", guid, lang).await
}

pub fn run() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() >= 2 {
        match args[1].as_str() {
            "--elevated" => std::process::exit(installer::elevated_main(&args[2..])),
            "--list" => {
                match outputs_now() {
                    Ok(o) => println!("{}", serde_json::to_string_pretty(&o).unwrap()),
                    Err(e) => { eprintln!("{e}"); std::process::exit(1); }
                }
                return;
            }
            _ => {}
        }
    }

    tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| {
            if let Some(w) = app.get_webview_window("main") {
                let _ = w.unminimize();
                let _ = w.show();
                let _ = w.set_focus();
            }
        }))
        .invoke_handler(tauri::generate_handler![
            list_outputs,
            get_config,
            set_config,
            get_status,
            launch_args,
            install_output,
            uninstall_output
        ])
        .run(tauri::generate_context!())
        .expect("erro ao iniciar o Musix");
}
