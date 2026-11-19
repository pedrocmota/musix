use std::time::{Duration, Instant};
use windows::core::PCWSTR;
use windows::Win32::Foundation::{
    ERROR_MORE_DATA, ERROR_SERVICE_ALREADY_RUNNING, ERROR_SERVICE_CANNOT_ACCEPT_CTRL, ERROR_SERVICE_NOT_ACTIVE,
};
use windows::Win32::System::Services::{
    CloseServiceHandle, ControlService, EnumDependentServicesW, OpenSCManagerW, OpenServiceW, QueryServiceStatus,
    StartServiceW, ENUM_SERVICE_STATUSW, SC_HANDLE, SC_MANAGER_CONNECT, SERVICE_ACTIVE, SERVICE_CONTROL_STOP,
    SERVICE_ENUMERATE_DEPENDENTS, SERVICE_QUERY_STATUS, SERVICE_RUNNING, SERVICE_START, SERVICE_STATUS,
    SERVICE_STATUS_CURRENT_STATE, SERVICE_STOP, SERVICE_STOPPED, SERVICE_STOP_PENDING,
};

use crate::sec::wide;

struct Scm(SC_HANDLE);
impl Drop for Scm {
    fn drop(&mut self) {
        unsafe { let _ = CloseServiceHandle(self.0); }
    }
}

fn open_scm() -> Result<Scm, String> {
    unsafe { OpenSCManagerW(PCWSTR::null(), PCWSTR::null(), SC_MANAGER_CONNECT) }
        .map(Scm)
        .map_err(|e| format!("OpenSCManager: {e}"))
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

pub fn active_dependents(name: &str) -> Result<Vec<String>, String> {
    let scm = open_scm()?;
    let svc = open(&scm, name, SERVICE_ENUMERATE_DEPENDENTS)?;
    let entry = std::mem::size_of::<ENUM_SERVICE_STATUSW>();
    let mut buf: Vec<ENUM_SERVICE_STATUSW> = Vec::new();
    let mut needed = 0u32;
    let mut count = 0u32;
    for _ in 0..8 {
        let ptr = if buf.capacity() == 0 { None } else { Some(buf.as_mut_ptr()) };
        let cb = (buf.capacity() * entry) as u32;
        match unsafe { EnumDependentServicesW(svc.0, SERVICE_ACTIVE, ptr, cb, &mut needed, &mut count) } {
            Ok(()) => {
                unsafe { buf.set_len(count as usize) };
                let mut names = Vec::with_capacity(buf.len());
                for e in &buf {
                    let n = unsafe { e.lpServiceName.to_string() }.map_err(|e| format!("EnumDependentServices({name}): {e}"))?;
                    names.push(n);
                }
                return Ok(names);
            }
            Err(e) if e.code() == ERROR_MORE_DATA.to_hresult() => {
                buf = Vec::with_capacity((needed as usize).div_ceil(entry) + 1);
            }
            Err(e) => return Err(format!("EnumDependentServices({name}): {e}")),
        }
    }
    Err(format!("EnumDependentServices({name}): the list keeps changing"))
}

fn stop_one(scm: &Scm, name: &str) -> Result<(), String> {
    let svc = open(scm, name, SERVICE_STOP | SERVICE_QUERY_STATUS)?;
    let mut st = SERVICE_STATUS::default();
    match unsafe { ControlService(svc.0, SERVICE_CONTROL_STOP, &mut st) } {
        Ok(()) => {}
        Err(e) if e.code() == ERROR_SERVICE_NOT_ACTIVE.to_hresult() => return Ok(()),
        Err(e) if e.code() == ERROR_SERVICE_CANNOT_ACCEPT_CTRL.to_hresult() && state(&svc)? == SERVICE_STOP_PENDING => {}
        Err(e) => return Err(format!("ControlService(stop, {name}): {e}")),
    }
    wait_for(&svc, SERVICE_STOPPED, Duration::from_secs(20)).map_err(|e| format!("stop {name}: {e}"))
}

pub fn stop(name: &str, stopped: &mut Vec<String>) -> Result<(), String> {
    let scm = open_scm()?;
    for dep in active_dependents(name)? {
        stopped.push(dep.clone());
        stop_one(&scm, &dep)?;
    }
    stop_one(&scm, name)
}

pub fn start(name: &str) -> Result<(), String> {
    let scm = open_scm()?;
    let svc = open(&scm, name, SERVICE_START | SERVICE_QUERY_STATUS)?;
    if state(&svc)? == SERVICE_STOP_PENDING {
        wait_for(&svc, SERVICE_STOPPED, Duration::from_secs(20)).map_err(|e| format!("start {name}: {e}"))?;
    }
    match unsafe { StartServiceW(svc.0, None) } {
        Ok(()) => {}
        Err(e) if e.code() == ERROR_SERVICE_ALREADY_RUNNING.to_hresult() => return Ok(()),
        Err(e) => return Err(format!("StartService({name}): {e}")),
    }
    wait_for(&svc, SERVICE_RUNNING, Duration::from_secs(20)).map_err(|e| format!("start {name}: {e}"))
}

pub fn stop_audio(stopped: &mut Vec<String>) -> Result<(), String> {
    stop("Audiosrv", stopped)?;
    stop("AudioEndpointBuilder", stopped)
}

pub fn start_audio(dependents: &[String]) -> Result<Vec<String>, String> {
    start("AudioEndpointBuilder")?;
    start("Audiosrv")?;
    let mut warnings = Vec::new();
    for d in dependents.iter().rev() {
        if let Err(e) = start(d) {
            warnings.push(e);
        }
    }
    Ok(warnings)
}
