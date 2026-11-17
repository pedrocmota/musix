//! Reinício do áudio do Windows (AudioEndpointBuilder + Audiosrv) pela API do
//! Gerenciador de Serviços. Só usado pelo processo elevado.

use std::time::{Duration, Instant};
use windows::core::PCWSTR;
use windows::Win32::Foundation::{ERROR_SERVICE_ALREADY_RUNNING, ERROR_SERVICE_NOT_ACTIVE};
use windows::Win32::System::Services::{
    CloseServiceHandle, ControlService, OpenSCManagerW, OpenServiceW, QueryServiceStatus, StartServiceW, SC_HANDLE,
    SC_MANAGER_CONNECT, SERVICE_CONTROL_STOP, SERVICE_QUERY_STATUS, SERVICE_RUNNING, SERVICE_START, SERVICE_STATUS,
    SERVICE_STATUS_CURRENT_STATE, SERVICE_STOP, SERVICE_STOPPED,
};

use crate::sec::wide;

struct Scm(SC_HANDLE);
impl Drop for Scm {
    fn drop(&mut self) {
        unsafe { let _ = CloseServiceHandle(self.0); }
    }
}

fn open(scm: &Scm, name: &str, access: u32) -> Result<Scm, String> {
    let w = wide(name);
    unsafe { OpenServiceW(scm.0, PCWSTR(w.as_ptr()), access).map(Scm).map_err(|e| format!("OpenService({name}): {e}")) }
}

fn state(svc: &Scm) -> Result<SERVICE_STATUS_CURRENT_STATE, String> {
    let mut st = SERVICE_STATUS::default();
    unsafe { QueryServiceStatus(svc.0, &mut st).map_err(|e| format!("QueryServiceStatus: {e}"))? };
    Ok(st.dwCurrentState)
}

fn wait_for(svc: &Scm, target: SERVICE_STATUS_CURRENT_STATE, timeout: Duration) -> Result<(), String> {
    let t0 = Instant::now();
    loop {
        if state(svc)? == target {
            return Ok(());
        }
        if t0.elapsed() > timeout {
            return Err("timeout waiting for service state".into());
        }
        std::thread::sleep(Duration::from_millis(150));
    }
}

pub fn stop(name: &str) -> Result<(), String> {
    let scm = unsafe { OpenSCManagerW(PCWSTR::null(), PCWSTR::null(), SC_MANAGER_CONNECT) }
        .map(Scm)
        .map_err(|e| format!("OpenSCManager: {e}"))?;
    let svc = open(&scm, name, SERVICE_STOP | SERVICE_QUERY_STATUS)?;
    let mut st = SERVICE_STATUS::default();
    match unsafe { ControlService(svc.0, SERVICE_CONTROL_STOP, &mut st) } {
        Ok(()) => {}
        Err(e) if e.code() == ERROR_SERVICE_NOT_ACTIVE.to_hresult() => return Ok(()),
        Err(e) => return Err(format!("ControlService(stop, {name}): {e}")),
    }
    wait_for(&svc, SERVICE_STOPPED, Duration::from_secs(20)).map_err(|e| format!("stop {name}: {e}"))
}

pub fn start(name: &str) -> Result<(), String> {
    let scm = unsafe { OpenSCManagerW(PCWSTR::null(), PCWSTR::null(), SC_MANAGER_CONNECT) }
        .map(Scm)
        .map_err(|e| format!("OpenSCManager: {e}"))?;
    let svc = open(&scm, name, SERVICE_START | SERVICE_QUERY_STATUS)?;
    match unsafe { StartServiceW(svc.0, None) } {
        Ok(()) => {}
        Err(e) if e.code() == ERROR_SERVICE_ALREADY_RUNNING.to_hresult() => return Ok(()),
        Err(e) => return Err(format!("StartService({name}): {e}")),
    }
    wait_for(&svc, SERVICE_RUNNING, Duration::from_secs(20)).map_err(|e| format!("start {name}: {e}"))
}

/// Para o áudio (descarrega os APOs do audiodg.exe).
pub fn stop_audio() -> Result<(), String> {
    stop("Audiosrv")?;
    stop("AudioEndpointBuilder")
}

/// Religa o áudio (os APOs são recriados com o registro novo).
pub fn start_audio() -> Result<(), String> {
    start("AudioEndpointBuilder")?;
    start("Audiosrv")
}
