use std::ffi::c_void;
use windows::core::PWSTR;
use windows::Win32::Devices::FunctionDiscovery::{PKEY_DeviceInterface_FriendlyName, PKEY_Device_FriendlyName};
use windows::Win32::Media::Audio::{eMultimedia, eRender, IMMDeviceEnumerator, MMDeviceEnumerator, DEVICE_STATE_ACTIVE};
use windows::Win32::System::Com::{
    CoCreateInstance, CoInitializeEx, CoTaskMemFree, CoUninitialize, CLSCTX_ALL, COINIT_MULTITHREADED, STGM_READ,
};

pub struct Endpoint {
    pub id: String,
    pub guid: String,
    pub name: String,
    pub iface: String,
    pub is_default: bool,
}

unsafe fn take_pwstr(p: PWSTR) -> String {
    let s = p.to_string().unwrap_or_default();
    CoTaskMemFree(Some(p.0 as *const c_void));
    s
}

pub fn list_render_endpoints() -> Result<Vec<Endpoint>, String> {
    unsafe {
        let hr = CoInitializeEx(None, COINIT_MULTITHREADED);
        let inited = hr.is_ok();
        let r = enumerate();
        if inited {
            CoUninitialize();
        }
        r.map_err(|e| format!("err.enumerate|{e}"))
    }
}

unsafe fn enumerate() -> windows::core::Result<Vec<Endpoint>> {
    let en: IMMDeviceEnumerator = CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL)?;
    let default_id = en
        .GetDefaultAudioEndpoint(eRender, eMultimedia)
        .ok()
        .and_then(|d| d.GetId().ok())
        .map(|p| take_pwstr(p));
    let col = en.EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE)?;
    let n = col.GetCount()?;
    let mut out = Vec::with_capacity(n as usize);
    for i in 0..n {
        let dev = col.Item(i)?;
        let id = take_pwstr(dev.GetId()?);
        let ps = dev.OpenPropertyStore(STGM_READ)?;
        let name = ps.GetValue(&PKEY_Device_FriendlyName).map(|v| v.to_string()).unwrap_or_default();
        let iface = ps.GetValue(&PKEY_DeviceInterface_FriendlyName).map(|v| v.to_string()).unwrap_or_default();
        let guid = id.rsplit("}.").next().unwrap_or("").to_string();
        let is_default = default_id.as_deref() == Some(id.as_str());
        out.push(Endpoint { id, guid, name, iface, is_default });
    }
    Ok(out)
}
