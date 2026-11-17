#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <audioenginebaseapo.h>
#include <audiomediatype.h>
#include <ks.h>
#include <ksmedia.h>
#include <strsafe.h>
#include <new>

#include "../dsp/musix_dsp.h"

static const CLSID CLSID_MusixMFX = { 0x6d7a2c5e, 0x9b1f, 0x4e0b, { 0x8a, 0x3d, 0x1f, 0x2e, 0x3c, 0x4b, 0x5a, 0x69 } };

static const wchar_t *APO_FRIENDLY  = L"Musix Karaoke MFX";
static const wchar_t *APO_COPYRIGHT = L"Musix";
static const UINT32   APO_FLAGS     = APO_FLAG_SAMPLESPERFRAME_MUST_MATCH | APO_FLAG_FRAMESPERSECOND_MUST_MATCH |
                                      APO_FLAG_BITSPERSAMPLE_MUST_MATCH | APO_FLAG_INPLACE;

static HMODULE g_hModule = NULL;
static volatile LONG g_objCount = 0;

static void GetMusixDir(wchar_t *out, size_t cch) {
    wchar_t pd[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"MUSIX_DIR", out, (DWORD)cch);
    if (n > 0 && n < cch) return;
    n = GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) StringCchCopyW(pd, MAX_PATH, L"C:\\ProgramData");
    StringCchPrintfW(out, cch, L"%s\\Musix", pd);
}

static void WriteTextFile(const wchar_t *path, const char *text) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(h, text, (DWORD)strlen(text), &w, NULL);
    CloseHandle(h);
}

static bool IsFloat32(IAudioMediaType *fmt, UNCOMPRESSEDAUDIOFORMAT *out) {
    if (!fmt) return false;
    UNCOMPRESSEDAUDIOFORMAT u;
    if (FAILED(fmt->GetUncompressedAudioFormat(&u))) return false;
    if (out) *out = u;
    return u.guidFormatType == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT &&
           u.dwBytesPerSampleContainer == 4 && u.dwValidBitsPerSample == 32 &&
           u.dwSamplesPerFrame >= 1 && u.fFramesPerSecond > 0;
}

class CMusixAPO : public IAudioProcessingObject,
                  public IAudioProcessingObjectRT,
                  public IAudioProcessingObjectConfiguration,
                  public IAudioSystemEffects {
public:
    struct NonDelegating : public IUnknown {
        CMusixAPO *self;
        STDMETHODIMP QueryInterface(REFIID riid, void **ppv) { return self->NonDelegatingQI(riid, ppv); }
        STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&self->m_ref); }
        STDMETHODIMP_(ULONG) Release() {
            LONG r = InterlockedDecrement(&self->m_ref);
            if (r == 0) delete self;
            return (ULONG)r;
        }
    } m_inner;

    explicit CMusixAPO(IUnknown *pOuter)
        : m_ref(1), m_initialized(false), m_locked(false), m_chain(NULL),
          m_thread(NULL), m_stop(NULL), m_lastWrite(0),
          m_cfgEnabled(1), m_cfgVC(0), m_cfgPitch(0), m_cfgOrder(0),
          m_curVC(0), m_curPitch(0), m_curOrder(0), m_rate(0), m_channels(0) {
        m_inner.self = this;
        m_pOuter = pOuter ? pOuter : static_cast<IUnknown *>(&m_inner);
        InterlockedIncrement(&g_objCount);
        InitializeCriticalSection(&m_cs);
        GetMusixDir(m_dir, MAX_PATH);
        StringCchPrintfW(m_cfgPath, MAX_PATH, L"%s\\musix.cfg", m_dir);
        StringCchPrintfW(m_statusPath, MAX_PATH, L"%s\\status.txt", m_dir);
        m_chain = new (std::nothrow) musix_chain_t;
        if (m_chain) musix_chain_init(m_chain);
    }

    virtual ~CMusixAPO() {
        StopConfigThread();
        delete m_chain;
        DeleteCriticalSection(&m_cs);
        InterlockedDecrement(&g_objCount);
    }

    HRESULT NonDelegatingQI(REFIID riid, void **ppv) {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown)) {
            *ppv = static_cast<IUnknown *>(&m_inner);
            m_inner.AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IAudioProcessingObject))
            *ppv = static_cast<IAudioProcessingObject *>(this);
        else if (riid == __uuidof(IAudioProcessingObjectRT))
            *ppv = static_cast<IAudioProcessingObjectRT *>(this);
        else if (riid == __uuidof(IAudioProcessingObjectConfiguration))
            *ppv = static_cast<IAudioProcessingObjectConfiguration *>(this);
        else if (riid == __uuidof(IAudioSystemEffects))
            *ppv = static_cast<IAudioSystemEffects *>(this);
        else { *ppv = NULL; return E_NOINTERFACE; }
        static_cast<IUnknown *>(*ppv)->AddRef();
        return S_OK;
    }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) { return m_pOuter->QueryInterface(riid, ppv); }
    STDMETHODIMP_(ULONG) AddRef() { return m_pOuter->AddRef(); }
    STDMETHODIMP_(ULONG) Release() { return m_pOuter->Release(); }

    STDMETHODIMP Reset() {
        EnterCriticalSection(&m_cs);
        if (m_chain) { musix_vc_reset(&m_chain->vc); musix_ps_reset(&m_chain->ps); }
        LeaveCriticalSection(&m_cs);
        return S_OK;
    }

    STDMETHODIMP GetLatency(HNSTIME *pTime) {
        if (!pTime) return E_POINTER;
        *pTime = 0;
        return S_OK;
    }

    STDMETHODIMP GetRegistrationProperties(APO_REG_PROPERTIES **ppRegProps) {
        if (!ppRegProps) return E_POINTER;
        APO_REG_PROPERTIES *p = (APO_REG_PROPERTIES *)CoTaskMemAlloc(sizeof(APO_REG_PROPERTIES));
        if (!p) return E_OUTOFMEMORY;
        FillRegProps(p);
        *ppRegProps = p;
        return S_OK;
    }

    STDMETHODIMP Initialize(UINT32 cbDataSize, BYTE *pbyData) {
        if (m_initialized) return APOERR_ALREADY_INITIALIZED;
        if (cbDataSize >= sizeof(APOInitBaseStruct) && pbyData) {
            const APOInitBaseStruct *b = reinterpret_cast<const APOInitBaseStruct *>(pbyData);
            if (b->clsid != CLSID_MusixMFX) return E_INVALIDARG;
        }
        m_initialized = true;
        return S_OK;
    }

    STDMETHODIMP IsInputFormatSupported(IAudioMediaType *pOutputFormat, IAudioMediaType *pRequestedInputFormat,
                                        IAudioMediaType **ppSupportedInputFormat) {
        return IsFormatSupported(pOutputFormat, pRequestedInputFormat, ppSupportedInputFormat);
    }

    STDMETHODIMP IsOutputFormatSupported(IAudioMediaType *pInputFormat, IAudioMediaType *pRequestedOutputFormat,
                                         IAudioMediaType **ppSupportedOutputFormat) {
        return IsFormatSupported(pInputFormat, pRequestedOutputFormat, ppSupportedOutputFormat);
    }

    STDMETHODIMP GetInputChannelCount(UINT32 *pu32ChannelCount) {
        if (!pu32ChannelCount) return E_POINTER;
        *pu32ChannelCount = (UINT32)m_channels;
        return S_OK;
    }

    STDMETHODIMP LockForProcess(UINT32 nIn, APO_CONNECTION_DESCRIPTOR **ppIn,
                                UINT32 nOut, APO_CONNECTION_DESCRIPTOR **ppOut) {
        if (!m_initialized) return APOERR_NOT_INITIALIZED;
        if (nIn != 1 || nOut != 1 || !ppIn || !ppOut || !ppIn[0] || !ppOut[0]) return E_INVALIDARG;

        UNCOMPRESSEDAUDIOFORMAT fin, fout;
        if (!IsFloat32(ppIn[0]->pFormat, &fin) || !IsFloat32(ppOut[0]->pFormat, &fout))
            return APOERR_INVALID_CONNECTION_FORMAT;
        if (fin.dwSamplesPerFrame != fout.dwSamplesPerFrame || fin.fFramesPerSecond != fout.fFramesPerSecond)
            return APOERR_INVALID_CONNECTION_FORMAT;

        EnterCriticalSection(&m_cs);
        m_rate = (int)(fin.fFramesPerSecond + 0.5f);
        m_channels = (int)fin.dwSamplesPerFrame;
        if (m_chain) {
            musix_chain_set_format(m_chain, m_rate, m_channels);
            m_curVC = -1; m_curPitch = 99; m_curOrder = -1;
        }
        m_locked = true;
        LeaveCriticalSection(&m_cs);

        LoadConfigFile();
        StartConfigThread();
        WriteStatus(true);
        return S_OK;
    }

    STDMETHODIMP UnlockForProcess() {
        if (!m_locked) return APOERR_ALREADY_UNLOCKED;
        StopConfigThread();
        EnterCriticalSection(&m_cs);
        m_locked = false;
        LeaveCriticalSection(&m_cs);
        WriteStatus(false);
        return S_OK;
    }

    STDMETHODIMP_(UINT32) CalcInputFrames(UINT32 u32OutputFrameCount) { return u32OutputFrameCount; }
    STDMETHODIMP_(UINT32) CalcOutputFrames(UINT32 u32InputFrameCount) { return u32InputFrameCount; }

    STDMETHODIMP_(void) APOProcess(UINT32 nIn, APO_CONNECTION_PROPERTY **ppIn,
                                   UINT32 nOut, APO_CONNECTION_PROPERTY **ppOut) {
        if (nIn < 1 || nOut < 1 || !ppIn || !ppOut || !ppIn[0] || !ppOut[0]) return;
        APO_CONNECTION_PROPERTY *in = ppIn[0];
        APO_CONNECTION_PROPERTY *out = ppOut[0];
        float *pin  = reinterpret_cast<float *>(in->pBuffer);
        float *pout = reinterpret_cast<float *>(out->pBuffer);
        UINT32 frames = in->u32ValidFrameCount;
        size_t bytes = (size_t)frames * (size_t)m_channels * sizeof(float);

        if (!m_chain || m_channels <= 0 || !m_locked) {
            if (pin != pout && m_channels > 0) memcpy(pout, pin, bytes);
            out->u32ValidFrameCount = frames;
            out->u32BufferFlags = in->u32BufferFlags;
            return;
        }

        int en    = (int)m_cfgEnabled;
        int vc    = en ? (int)m_cfgVC : 0;
        int pitch = en ? (int)m_cfgPitch : 0;
        int order = (int)m_cfgOrder;
        if (vc != m_curVC)       { musix_vc_set_enabled(&m_chain->vc, vc); m_curVC = vc; }
        if (pitch != m_curPitch) { musix_ps_set_semitone(&m_chain->ps, pitch); m_curPitch = pitch; }
        if (order != m_curOrder) { m_chain->order_ps_first = order; m_curOrder = order; }

        bool active = musix_vc_is_active(&m_chain->vc) || musix_ps_is_active(&m_chain->ps);
        if (!active) {
            if (pin != pout) memcpy(pout, pin, bytes);
            out->u32ValidFrameCount = frames;
            out->u32BufferFlags = in->u32BufferFlags;
            return;
        }

        if (in->u32BufferFlags == BUFFER_SILENT) {
            memset(pout, 0, bytes);
            musix_chain_process(m_chain, pout, pout, (int)frames);
        } else {
            musix_chain_process(m_chain, pin, pout, (int)frames);
        }
        out->u32ValidFrameCount = frames;
        out->u32BufferFlags = BUFFER_VALID;
    }

    static void FillRegProps(APO_REG_PROPERTIES *p) {
        memset(p, 0, sizeof(*p));
        p->clsid = CLSID_MusixMFX;
        p->Flags = (APO_FLAG)APO_FLAGS;
        StringCchCopyW(p->szFriendlyName, 256, APO_FRIENDLY);
        StringCchCopyW(p->szCopyrightInfo, 256, APO_COPYRIGHT);
        p->u32MajorVersion = 1;
        p->u32MinorVersion = 0;
        p->u32MinInputConnections = 1;
        p->u32MaxInputConnections = 1;
        p->u32MinOutputConnections = 1;
        p->u32MaxOutputConnections = 1;
        p->u32MaxInstances = 0xFFFFFFFF;
        p->u32NumAPOInterfaces = 1;
        p->iidAPOInterfaceList[0] = __uuidof(IAudioProcessingObject);
    }

private:
    HRESULT IsFormatSupported(IAudioMediaType *pOpposite, IAudioMediaType *pRequested, IAudioMediaType **ppSupported) {
        if (ppSupported) *ppSupported = NULL;
        if (!pRequested) return E_POINTER;
        UNCOMPRESSEDAUDIOFORMAT r, o;
        bool reqOk = IsFloat32(pRequested, &r);
        bool oppOk = IsFloat32(pOpposite, &o);
        if (reqOk && (!pOpposite || (oppOk && o.dwSamplesPerFrame == r.dwSamplesPerFrame && o.fFramesPerSecond == r.fFramesPerSecond))) {
            if (ppSupported) { *ppSupported = pRequested; pRequested->AddRef(); }
            return S_OK;
        }
        if (oppOk) {
            if (ppSupported) { *ppSupported = pOpposite; pOpposite->AddRef(); }
            return S_FALSE;
        }
        return APOERR_FORMAT_NOT_SUPPORTED;
    }

    static int ParseInt(const char *s) {
        int sign = 1, v = 0;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
        return sign * v;
    }

    void ApplyConfigLine(const char *line) {
        const char *eq = strchr(line, '=');
        if (!eq) return;
        size_t klen = (size_t)(eq - line);
        int v = ParseInt(eq + 1);
        if (klen == 7 && _strnicmp(line, "enabled", 7) == 0) {
            InterlockedExchange(&m_cfgEnabled, v ? 1 : 0);
        } else if (klen == 11 && _strnicmp(line, "voicecancel", 11) == 0) {
            InterlockedExchange(&m_cfgVC, v ? 1 : 0);
        } else if (klen == 5 && _strnicmp(line, "pitch", 5) == 0) {
            if (v > 4) v = 4;
            if (v < -4) v = -4;
            InterlockedExchange(&m_cfgPitch, v);
        } else if (klen == 5 && _strnicmp(line, "order", 5) == 0) {
            InterlockedExchange(&m_cfgOrder, v ? 1 : 0);
        }
    }

    void LoadConfigFile() {
        HANDLE h = CreateFileW(m_cfgPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return;
        char buf[4096];
        DWORD n = 0;
        BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &n, NULL);
        FILETIME ft = {0, 0};
        GetFileTime(h, NULL, NULL, &ft);
        CloseHandle(h);
        if (!ok) return;
        buf[n] = 0;
        m_lastWrite = ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

        char *p = buf;
        while (*p) {
            char *e = p;
            while (*e && *e != '\n' && *e != '\r') e++;
            char save = *e; *e = 0;
            ApplyConfigLine(p);
            *e = save;
            while (*e == '\n' || *e == '\r') e++;
            p = e;
        }
    }

    static DWORD WINAPI ConfigThreadProc(LPVOID arg) {
        CMusixAPO *self = static_cast<CMusixAPO *>(arg);
        while (WaitForSingleObject(self->m_stop, 150) == WAIT_TIMEOUT) {
            WIN32_FILE_ATTRIBUTE_DATA fad;
            if (!GetFileAttributesExW(self->m_cfgPath, GetFileExInfoStandard, &fad)) continue;
            ULONGLONG wt = ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime;
            if (wt != self->m_lastWrite) self->LoadConfigFile();
        }
        return 0;
    }

    void StartConfigThread() {
        if (m_thread) return;
        m_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!m_stop) return;
        m_thread = CreateThread(NULL, 0, ConfigThreadProc, this, 0, NULL);
        if (!m_thread) { CloseHandle(m_stop); m_stop = NULL; }
    }

    void StopConfigThread() {
        if (m_thread) {
            SetEvent(m_stop);
            WaitForSingleObject(m_thread, 2000);
            CloseHandle(m_thread); m_thread = NULL;
        }
        if (m_stop) { CloseHandle(m_stop); m_stop = NULL; }
    }

    void WriteStatus(bool locked) {
        char text[512];
        SYSTEMTIME st; GetLocalTime(&st);
        StringCchPrintfA(text, sizeof(text),
            "locked=%d\r\nrate=%d\r\nchannels=%d\r\npid=%lu\r\ntime=%04d-%02d-%02d %02d:%02d:%02d\r\n",
            locked ? 1 : 0, m_rate, m_channels, GetCurrentProcessId(),
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        WriteTextFile(m_statusPath, text);
    }

    LONG              m_ref;
    IUnknown         *m_pOuter;
    bool              m_initialized, m_locked;
    CRITICAL_SECTION  m_cs;
    musix_chain_t    *m_chain;
    HANDLE            m_thread, m_stop;
    ULONGLONG         m_lastWrite;
    volatile LONG     m_cfgEnabled, m_cfgVC, m_cfgPitch, m_cfgOrder;
    int               m_curVC, m_curPitch, m_curOrder;
    int               m_rate, m_channels;
    wchar_t           m_dir[MAX_PATH], m_cfgPath[MAX_PATH], m_statusPath[MAX_PATH];
};

class CMusixFactory : public IClassFactory {
public:
    CMusixFactory() : m_ref(1) { InterlockedIncrement(&g_objCount); }
    ~CMusixFactory() { InterlockedDecrement(&g_objCount); }

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) {
            *ppv = static_cast<IClassFactory *>(this); AddRef(); return S_OK;
        }
        *ppv = NULL; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() {
        LONG r = InterlockedDecrement(&m_ref);
        if (r == 0) delete this;
        return (ULONG)r;
    }
    STDMETHODIMP CreateInstance(IUnknown *pOuter, REFIID riid, void **ppv) {
        if (!ppv) return E_POINTER;
        *ppv = NULL;
        if (pOuter && riid != __uuidof(IUnknown)) return CLASS_E_NOAGGREGATION;
        CMusixAPO *apo = new (std::nothrow) CMusixAPO(pOuter);
        if (!apo) return E_OUTOFMEMORY;
        if (pOuter) {
            *ppv = static_cast<IUnknown *>(&apo->m_inner);
            return S_OK;
        }
        HRESULT hr = apo->m_inner.QueryInterface(riid, ppv);
        apo->m_inner.Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) {
        if (fLock) InterlockedIncrement(&g_objCount); else InterlockedDecrement(&g_objCount);
        return S_OK;
    }
private:
    LONG m_ref;
};

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = (HMODULE)hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (rclsid != CLSID_MusixMFX) return CLASS_E_CLASSNOTAVAILABLE;
    CMusixFactory *f = new (std::nothrow) CMusixFactory();
    if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv);
    f->Release();
    return hr;
}

STDAPI DllCanUnloadNow() {
    return g_objCount == 0 ? S_OK : S_FALSE;
}

static LONG RegSetStr(HKEY k, const wchar_t *name, const wchar_t *val) {
    return RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)val, (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
}
static LONG RegSetDw(HKEY k, const wchar_t *name, DWORD val) {
    return RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&val, sizeof(val));
}

STDAPI DllRegisterServer() {
    wchar_t clsid[64], iid[64], path[MAX_PATH], sub[256];
    HKEY k;
    LONG r;
    StringFromGUID2(CLSID_MusixMFX, clsid, 64);
    StringFromGUID2(__uuidof(IAudioProcessingObject), iid, 64);
    GetModuleFileNameW(g_hModule, path, MAX_PATH);

    StringCchPrintfW(sub, 256, L"SOFTWARE\\Classes\\CLSID\\%s", clsid);
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, sub, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    RegSetStr(k, NULL, APO_FRIENDLY);
    RegCloseKey(k);
    StringCchPrintfW(sub, 256, L"SOFTWARE\\Classes\\CLSID\\%s\\InprocServer32", clsid);
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, sub, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    RegSetStr(k, NULL, path);
    RegSetStr(k, L"ThreadingModel", L"Both");
    RegCloseKey(k);

    StringCchPrintfW(sub, 256, L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\%s", clsid);
    r = RegCreateKeyExW(HKEY_LOCAL_MACHINE, sub, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL);
    if (r != ERROR_SUCCESS) return HRESULT_FROM_WIN32(r);
    RegSetStr(k, L"FriendlyName", APO_FRIENDLY);
    RegSetStr(k, L"Copyright", APO_COPYRIGHT);
    RegSetDw(k, L"MajorVersion", 1);
    RegSetDw(k, L"MinorVersion", 0);
    RegSetDw(k, L"Flags", APO_FLAGS);
    RegSetDw(k, L"MinInputConnections", 1);
    RegSetDw(k, L"MaxInputConnections", 1);
    RegSetDw(k, L"MinOutputConnections", 1);
    RegSetDw(k, L"MaxOutputConnections", 1);
    RegSetDw(k, L"MaxInstances", 0xFFFFFFFF);
    RegSetDw(k, L"NumAPOInterfaces", 1);
    RegSetStr(k, L"APOInterface0", iid);
    RegCloseKey(k);
    return S_OK;
}

STDAPI DllUnregisterServer() {
    wchar_t clsid[64], sub[256];
    StringFromGUID2(CLSID_MusixMFX, clsid, 64);
    StringCchPrintfW(sub, 256, L"SOFTWARE\\Classes\\AudioEngine\\AudioProcessingObjects\\%s", clsid);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, sub);
    StringCchPrintfW(sub, 256, L"SOFTWARE\\Classes\\CLSID\\%s", clsid);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, sub);
    return S_OK;
}
