use std::path::Path;
use windows::core::PCWSTR;
use windows::Win32::Foundation::{CloseHandle, ERROR_CANCELLED};
use windows::Win32::System::Threading::{GetExitCodeProcess, WaitForSingleObject, INFINITE};
use windows::Win32::UI::Shell::{ShellExecuteExW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW};
use windows::Win32::UI::WindowsAndMessaging::SW_HIDE;

use crate::sec::wide;

pub fn run_elevated(exe: &Path, args: &str) -> Result<i32, String> {
    let file = wide(&exe.to_string_lossy());
    let params = wide(args);
    let verb = wide("runas");
    let mut info = SHELLEXECUTEINFOW {
        cbSize: std::mem::size_of::<SHELLEXECUTEINFOW>() as u32,
        fMask: SEE_MASK_NOCLOSEPROCESS,
        lpVerb: PCWSTR(verb.as_ptr()),
        lpFile: PCWSTR(file.as_ptr()),
        lpParameters: PCWSTR(params.as_ptr()),
        nShow: SW_HIDE.0,
        ..Default::default()
    };
    unsafe {
        if let Err(e) = ShellExecuteExW(&mut info) {
            if e.code() == ERROR_CANCELLED.to_hresult() {
                return Err("err.uac_cancelled".into());
            }
            return Err(format!("err.elevate|{e}"));
        }
        if info.hProcess.is_invalid() {
            return Err("err.elevated_process".into());
        }
        WaitForSingleObject(info.hProcess, INFINITE);
        let mut code: u32 = 0;
        let _ = GetExitCodeProcess(info.hProcess, &mut code);
        let _ = CloseHandle(info.hProcess);
        Ok(code as i32)
    }
}
