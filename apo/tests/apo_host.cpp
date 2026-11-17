#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioenginebaseapo.h>
#include <audiomediatype.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../dsp/musix_dsp.h"

static const CLSID CLSID_MusixMFX = { 0x6d7a2c5e, 0x9b1f, 0x4e0b, { 0x8a, 0x3d, 0x1f, 0x2e, 0x3c, 0x4b, 0x5a, 0x69 } };

#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FALHA: %s\n", msg); return 1; } } while (0)

class FakeMediaType : public IAudioMediaType {
public:
    FakeMediaType(int rate, int ch) : m_ref(1) {
        memset(&m_wfx, 0, sizeof(m_wfx));
        m_wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        m_wfx.Format.nChannels = (WORD)ch;
        m_wfx.Format.nSamplesPerSec = rate;
        m_wfx.Format.wBitsPerSample = 32;
        m_wfx.Format.nBlockAlign = (WORD)(ch * 4);
        m_wfx.Format.nAvgBytesPerSec = rate * ch * 4;
        m_wfx.Format.cbSize = 22;
        m_wfx.Samples.wValidBitsPerSample = 32;
        m_wfx.dwChannelMask = ch == 2 ? 3 : 0;
        m_wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        m_u.guidFormatType = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        m_u.dwSamplesPerFrame = ch;
        m_u.dwBytesPerSampleContainer = 4;
        m_u.dwValidBitsPerSample = 32;
        m_u.fFramesPerSecond = (FLOAT)rate;
        m_u.dwChannelMask = m_wfx.dwChannelMask;
    }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioMediaType)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = NULL; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() { LONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return (ULONG)r; }
    STDMETHODIMP IsCompressedFormat(BOOL *p) { *p = FALSE; return S_OK; }
    STDMETHODIMP IsEqual(IAudioMediaType *, DWORD *p) { *p = 0xFFFFFFFF; return S_OK; }
    virtual ~FakeMediaType() {}
    const WAVEFORMATEX *STDMETHODCALLTYPE GetAudioFormat() { return &m_wfx.Format; }
    STDMETHODIMP GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT *p) { *p = m_u; return S_OK; }
private:
    LONG m_ref;
    WAVEFORMATEXTENSIBLE m_wfx;
    UNCOMPRESSEDAUDIOFORMAT m_u;
};

static void WriteCfg(const wchar_t *dir, const char *text) {
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s\\musix.cfg", dir);
    FILE *f = _wfopen(p, L"wb");
    fputs(text, f);
    fclose(f);
}

typedef HRESULT (STDAPICALLTYPE *PFN_DGCO)(REFCLSID, REFIID, void **);

class Outer : public IUnknown {
public:
    Outer() : m_ref(1), m_innerUnk(NULL) {}
    LONG m_ref; IUnknown *m_innerUnk;
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) {
        if (riid == __uuidof(IUnknown)) { *ppv = this; AddRef(); return S_OK; }
        return m_innerUnk ? m_innerUnk->QueryInterface(riid, ppv) : E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() { return (ULONG)InterlockedDecrement(&m_ref); }
};

static int TestAggregation(IClassFactory *cf) {
    Outer outer;
    IUnknown *inner = NULL;
    CHECK(cf->CreateInstance(&outer, __uuidof(IAudioProcessingObjectRT), (void **)&inner) == CLASS_E_NOAGGREGATION, "agregado so com IID_IUnknown");
    CHECK(SUCCEEDED(cf->CreateInstance(&outer, __uuidof(IUnknown), (void **)&inner)) && inner, "CreateInstance agregado");
    outer.m_innerUnk = inner;
    LONG before = outer.m_ref;
    IAudioProcessingObject *apo = NULL;
    CHECK(SUCCEEDED(inner->QueryInterface(__uuidof(IAudioProcessingObject), (void **)&apo)), "QI via inner");
    CHECK(outer.m_ref == before + 1, "AddRef da interface deve ir para o agregador");
    IUnknown *unk = NULL;
    CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IUnknown), (void **)&unk)) && unk == static_cast<IUnknown *>(&outer), "QI IUnknown pela interface deve devolver o agregador");
    unk->Release();
    apo->Release();
    CHECK(outer.m_ref == before, "Release da interface deve voltar ao agregador");
    IUnknown *unk2 = NULL;
    CHECK(SUCCEEDED(inner->QueryInterface(__uuidof(IUnknown), (void **)&unk2)) && unk2 == inner, "QI IUnknown no inner devolve o inner");
    unk2->Release();
    inner->Release();
    printf("agregacao COM: ok\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "uso: apo_host MusixAPO.dll\n"); return 2; }

    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    wcscat_s(dir, L"musix_apo_host");
    CreateDirectoryW(dir, NULL);
    SetEnvironmentVariableW(L"MUSIX_DIR", dir);
    WriteCfg(dir, "enabled=1\r\nvoicecancel=1\r\npitch=2\r\n");

    HMODULE h = LoadLibraryA(argv[1]);
    CHECK(h, "LoadLibrary");
    PFN_DGCO dgco = (PFN_DGCO)GetProcAddress(h, "DllGetClassObject");
    CHECK(dgco, "DllGetClassObject ausente");

    IClassFactory *cf = NULL;
    CHECK(SUCCEEDED(dgco(CLSID_MusixMFX, __uuidof(IClassFactory), (void **)&cf)), "fabrica");
    if (TestAggregation(cf) != 0) return 1;
    IAudioProcessingObject *apo = NULL;
    CHECK(SUCCEEDED(cf->CreateInstance(NULL, __uuidof(IAudioProcessingObject), (void **)&apo)), "CreateInstance");
    IAudioProcessingObjectRT *rt = NULL;
    IAudioProcessingObjectConfiguration *cfg = NULL;
    IAudioSystemEffects *fx = NULL;
    CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioProcessingObjectRT), (void **)&rt)), "QI RT");
    CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration), (void **)&cfg)), "QI Configuration");
    CHECK(SUCCEEDED(apo->QueryInterface(__uuidof(IAudioSystemEffects), (void **)&fx)), "QI IAudioSystemEffects");

    APO_REG_PROPERTIES *props = NULL;
    CHECK(SUCCEEDED(apo->GetRegistrationProperties(&props)) && props->clsid == CLSID_MusixMFX, "GetRegistrationProperties");
    printf("APO: %ls  flags=0x%x\n", props->szFriendlyName, props->Flags);
    CoTaskMemFree(props);

    APOInitBaseStruct init = { sizeof(init), CLSID_MusixMFX };
    CHECK(SUCCEEDED(apo->Initialize(sizeof(init), (BYTE *)&init)), "Initialize");
    CHECK(apo->Initialize(sizeof(init), (BYTE *)&init) == APOERR_ALREADY_INITIALIZED, "Initialize duplo");

    const int rate = 48000, ch = 2, block = 480, total = 48000;
    FakeMediaType *fmt = new FakeMediaType(rate, ch);
    IAudioMediaType *sup = NULL;
    CHECK(apo->IsInputFormatSupported(fmt, fmt, &sup) == S_OK && sup == fmt, "IsInputFormatSupported");
    sup->Release();
    CHECK(apo->IsOutputFormatSupported(fmt, fmt, &sup) == S_OK, "IsOutputFormatSupported");
    sup->Release();

    float *buf = (float *)_aligned_malloc((size_t)block * ch * sizeof(float), 16);
    APO_CONNECTION_DESCRIPTOR din = { APO_CONNECTION_BUFFER_TYPE_EXTERNAL, (UINT_PTR)buf, block, fmt, APO_CONNECTION_DESCRIPTOR_SIGNATURE };
    APO_CONNECTION_DESCRIPTOR dout = din;
    APO_CONNECTION_DESCRIPTOR *pin[1] = { &din }, *pout[1] = { &dout };
    CHECK(rt->CalcInputFrames(480) == 480 && rt->CalcOutputFrames(480) == 480, "CalcFrames");
    CHECK(cfg->LockForProcess(1, pin, 1, pout) == S_OK, "LockForProcess");

    musix_chain_t *ref = new musix_chain_t;
    musix_chain_init(ref);
    musix_chain_set_format(ref, rate, ch);
    musix_vc_set_enabled(&ref->vc, 1);
    musix_ps_set_semitone(&ref->ps, 2);

    float *sig = new float[(size_t)total * ch];
    for (int n = 0; n < total; n++) {
        double t = (double)n / rate;
        sig[n * 2]     = (float)(0.5 * sin(2 * 3.141592653589793 * 1000 * t) + 0.3 * sin(2 * 3.141592653589793 * 300 * t));
        sig[n * 2 + 1] = (float)(0.5 * sin(2 * 3.141592653589793 * 1000 * t) + 0.2 * sin(2 * 3.141592653589793 * 30 * t));
    }
    float *exp = new float[(size_t)total * ch];
    memcpy(exp, sig, (size_t)total * ch * sizeof(float));

    APO_CONNECTION_PROPERTY cin = { (UINT_PTR)buf, block, BUFFER_VALID, APO_CONNECTION_PROPERTY_SIGNATURE };
    APO_CONNECTION_PROPERTY cout_ = cin;
    APO_CONNECTION_PROPERTY *ppin[1] = { &cin }, *ppout[1] = { &cout_ };

    int mism = 0, changed_at = -1;
    for (int n = 0; n < total; n += block) {
        if (n == total / 2) {
            WriteCfg(dir, "enabled=1\r\nvoicecancel=0\r\npitch=-1\r\n");
            Sleep(600);
            musix_vc_set_enabled(&ref->vc, 0);
            musix_ps_set_semitone(&ref->ps, -1);
            changed_at = n;
        }
        memcpy(buf, sig + (size_t)n * ch, (size_t)block * ch * sizeof(float));
        cin.u32ValidFrameCount = block; cin.u32BufferFlags = BUFFER_VALID;
        rt->APOProcess(1, ppin, 1, ppout);
        CHECK(cout_.u32ValidFrameCount == (UINT32)block && cout_.u32BufferFlags == BUFFER_VALID, "flags de saida");
        musix_chain_process(ref, exp + (size_t)n * ch, exp + (size_t)n * ch, block);
        if (memcmp(buf, exp + (size_t)n * ch, (size_t)block * ch * sizeof(float)) != 0) mism++;
    }
    printf("blocos: %d, divergentes: %d (mudanca de parametro no bloco %d)\n", total / block, mism, changed_at / block);
    CHECK(mism == 0, "saida do APO difere do nucleo DSP");

    cin.u32BufferFlags = BUFFER_SILENT;
    rt->APOProcess(1, ppin, 1, ppout);
    CHECK(cout_.u32BufferFlags == BUFFER_VALID, "silencio -> saida valida");

    CHECK(cfg->UnlockForProcess() == S_OK, "UnlockForProcess");

    wchar_t sp[MAX_PATH];
    swprintf_s(sp, L"%s\\status.txt", dir);
    CHECK(GetFileAttributesW(sp) != INVALID_FILE_ATTRIBUTES, "status.txt nao foi escrito");

    fx->Release(); cfg->Release(); rt->Release(); apo->Release(); cf->Release(); fmt->Release();
    typedef HRESULT (STDAPICALLTYPE *PFN_CUN)();
    PFN_CUN cun = (PFN_CUN)GetProcAddress(h, "DllCanUnloadNow");
    CHECK(cun && cun() == S_OK, "DllCanUnloadNow apos liberar tudo");
    printf("OK: APO carrega, negocia float32, processa bit-exato e le a config ao vivo.\n");
    return 0;
}
