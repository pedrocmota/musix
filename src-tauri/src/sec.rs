//! Privilégios, posse e ACLs (registro e arquivos), registro COM da DLL.
//! Só usado pelo processo elevado.

use std::ffi::c_void;
use std::path::Path;
use windows::core::{HRESULT, PCSTR, PCWSTR, PWSTR};
use windows::Win32::Foundation::{CloseHandle, FreeLibrary, LocalFree, HANDLE, HLOCAL, LUID};
use windows::Win32::Security::Authorization::{
    GetNamedSecurityInfoW, SetEntriesInAclW, SetNamedSecurityInfoW, EXPLICIT_ACCESS_W, GRANT_ACCESS,
    NO_MULTIPLE_TRUSTEE, SE_FILE_OBJECT, SE_OBJECT_TYPE, SE_REGISTRY_KEY, TRUSTEE_IS_GROUP, TRUSTEE_IS_SID, TRUSTEE_W,
};
use windows::Win32::Security::{
    AdjustTokenPrivileges, CreateWellKnownSid, LookupPrivilegeValueW, WinBuiltinAdministratorsSid, WinBuiltinUsersSid,
    WinLocalServiceSid, ACE_FLAGS, ACL, CONTAINER_INHERIT_ACE, DACL_SECURITY_INFORMATION, LUID_AND_ATTRIBUTES,
    OBJECT_INHERIT_ACE, OWNER_SECURITY_INFORMATION, PSECURITY_DESCRIPTOR, PSID, SE_PRIVILEGE_ENABLED,
    TOKEN_ADJUST_PRIVILEGES, TOKEN_PRIVILEGES, TOKEN_QUERY, WELL_KNOWN_SID_TYPE,
};
use windows::Win32::System::LibraryLoader::{GetProcAddress, LoadLibraryW};
use windows::Win32::System::Threading::{GetCurrentProcess, OpenProcessToken};

pub fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// Acesso "Modificar" de arquivos/pastas (o mesmo M do icacls).
pub const FILE_MODIFY: u32 = 0x001301BF;
/// KEY_ALL_ACCESS
pub const KEY_ALL: u32 = 0x000F003F;

pub fn enable_privilege(name: &str) -> Result<(), String> {
    unsafe {
        let mut token = HANDLE::default();
        OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &mut token)
            .map_err(|e| format!("OpenProcessToken: {e}"))?;
        let mut luid = LUID::default();
        let r = LookupPrivilegeValueW(PCWSTR::null(), PCWSTR(wide(name).as_ptr()), &mut luid);
        if let Err(e) = r {
            let _ = CloseHandle(token);
            return Err(format!("LookupPrivilegeValue({name}): {e}"));
        }
        let tp = TOKEN_PRIVILEGES {
            PrivilegeCount: 1,
            Privileges: [LUID_AND_ATTRIBUTES { Luid: luid, Attributes: SE_PRIVILEGE_ENABLED }],
        };
        let r = AdjustTokenPrivileges(token, false, Some(&tp), 0, None, None);
        let _ = CloseHandle(token);
        r.map_err(|e| format!("AdjustTokenPrivileges({name}): {e}"))
    }
}

pub fn well_known_sid(kind: WELL_KNOWN_SID_TYPE) -> Result<Vec<u8>, String> {
    unsafe {
        let mut buf = vec![0u8; 68]; // SECURITY_MAX_SID_SIZE
        let mut size = buf.len() as u32;
        CreateWellKnownSid(kind, None, Some(PSID(buf.as_mut_ptr() as *mut c_void)), &mut size)
            .map_err(|e| format!("CreateWellKnownSid: {e}"))?;
        buf.truncate(size as usize);
        Ok(buf)
    }
}

pub fn sid_admins() -> Result<Vec<u8>, String> { well_known_sid(WinBuiltinAdministratorsSid) }
pub fn sid_users() -> Result<Vec<u8>, String> { well_known_sid(WinBuiltinUsersSid) }
pub fn sid_local_service() -> Result<Vec<u8>, String> { well_known_sid(WinLocalServiceSid) }

/// Torna Administradores dono do objeto (chave de registro "MACHINE\..." ou arquivo).
pub fn take_ownership(obj_name: &str, obj_type: SE_OBJECT_TYPE) -> Result<(), String> {
    let sid = sid_admins()?;
    let name = wide(obj_name);
    unsafe {
        SetNamedSecurityInfoW(
            PCWSTR(name.as_ptr()),
            obj_type,
            OWNER_SECURITY_INFORMATION,
            Some(PSID(sid.as_ptr() as *mut c_void)),
            None,
            None,
            None,
        )
        .ok()
        .map_err(|e| format!("SetNamedSecurityInfo(owner, {obj_name}): {e}"))
    }
}

/// Acrescenta uma ACE de permissão (herdável) à DACL do objeto.
pub fn grant(obj_name: &str, obj_type: SE_OBJECT_TYPE, sid: &[u8], access: u32, inheritance: ACE_FLAGS) -> Result<(), String> {
    let name = wide(obj_name);
    unsafe {
        let mut old_dacl: *mut ACL = std::ptr::null_mut();
        let mut sd = PSECURITY_DESCRIPTOR::default();
        GetNamedSecurityInfoW(
            PCWSTR(name.as_ptr()),
            obj_type,
            DACL_SECURITY_INFORMATION,
            None,
            None,
            Some(&mut old_dacl),
            None,
            &mut sd,
        )
        .ok()
        .map_err(|e| format!("GetNamedSecurityInfo(dacl, {obj_name}): {e}"))?;

        let ea = EXPLICIT_ACCESS_W {
            grfAccessPermissions: access,
            grfAccessMode: GRANT_ACCESS,
            grfInheritance: inheritance,
            Trustee: TRUSTEE_W {
                pMultipleTrustee: std::ptr::null_mut(),
                MultipleTrusteeOperation: NO_MULTIPLE_TRUSTEE,
                TrusteeForm: TRUSTEE_IS_SID,
                TrusteeType: TRUSTEE_IS_GROUP,
                ptstrName: PWSTR(sid.as_ptr() as *mut u16),
            },
        };
        let mut new_dacl: *mut ACL = std::ptr::null_mut();
        let r = SetEntriesInAclW(Some(&[ea]), Some(old_dacl as *const ACL), &mut new_dacl).ok();
        if let Err(e) = r {
            let _ = LocalFree(Some(HLOCAL(sd.0)));
            return Err(format!("SetEntriesInAcl({obj_name}): {e}"));
        }
        let r = SetNamedSecurityInfoW(
            PCWSTR(name.as_ptr()),
            obj_type,
            DACL_SECURITY_INFORMATION,
            None,
            None,
            Some(new_dacl as *const ACL),
            None,
        )
        .ok();
        let _ = LocalFree(Some(HLOCAL(new_dacl as *mut c_void)));
        let _ = LocalFree(Some(HLOCAL(sd.0)));
        r.map_err(|e| format!("SetNamedSecurityInfo(dacl, {obj_name}): {e}"))
    }
}

/// Chave HKLM: posse para Administradores + controle total, com herança.
pub fn own_registry_key(subkey: &str) -> Result<(), String> {
    let name = format!("MACHINE\\{subkey}");
    take_ownership(&name, SE_REGISTRY_KEY)?;
    grant(&name, SE_REGISTRY_KEY, &sid_admins()?, KEY_ALL, CONTAINER_INHERIT_ACE)
}

/// Pasta: Usuários e LOCAL SERVICE (dono do audiodg.exe) podem ler e gravar.
pub fn open_data_dir(path: &Path) -> Result<(), String> {
    let name = path.to_string_lossy().to_string();
    let inh = ACE_FLAGS(CONTAINER_INHERIT_ACE.0 | OBJECT_INHERIT_ACE.0);
    grant(&name, SE_FILE_OBJECT, &sid_users()?, FILE_MODIFY, inh)?;
    grant(&name, SE_FILE_OBJECT, &sid_local_service()?, FILE_MODIFY, inh)
}

/// Chama DllRegisterServer / DllUnregisterServer da DLL.
pub fn dll_register(path: &Path, register: bool) -> Result<(), String> {
    let w = wide(&path.to_string_lossy());
    unsafe {
        let h = LoadLibraryW(PCWSTR(w.as_ptr())).map_err(|e| format!("LoadLibrary({}): {e}", path.display()))?;
        let fname: &[u8] = if register { b"DllRegisterServer\0" } else { b"DllUnregisterServer\0" };
        let f = GetProcAddress(h, PCSTR(fname.as_ptr()));
        let r = match f {
            Some(f) => {
                let f: unsafe extern "system" fn() -> HRESULT = std::mem::transmute(f);
                let hr = f();
                if hr.is_ok() { Ok(()) } else { Err(format!("{}: 0x{:08X}", std::str::from_utf8(&fname[..fname.len() - 1]).unwrap_or(""), hr.0 as u32)) }
            }
            None => Err("GetProcAddress(DllRegisterServer): export not found".into()),
        };
        let _ = FreeLibrary(h);
        r
    }
}
