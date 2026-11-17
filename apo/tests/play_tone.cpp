#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <string>

#define CHECK(hr, msg) do { if (FAILED(hr)) { fprintf(stderr, "FALHA %s: 0x%08lx\n", msg, (unsigned long)(hr)); return 1; } } while (0)

static IMMDevice *FindDevice(IMMDeviceEnumerator *en, const wchar_t *needle) {
    IMMDeviceCollection *col = NULL;
    if (FAILED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) return NULL;
    UINT n = 0; col->GetCount(&n);
    IMMDevice *found = NULL;
    for (UINT i = 0; i < n && !found; i++) {
        IMMDevice *d = NULL; col->Item(i, &d);
        LPWSTR id = NULL; d->GetId(&id);
        if (id && wcsstr(id, needle)) { found = d; found->AddRef(); }
        CoTaskMemFree(id);
        d->Release();
    }
    col->Release();
    return found;
}

int wmain(int argc, wchar_t **argv) {
    if (argc >= 2 && wcscmp(argv[1], L"list") == 0) {
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        IMMDeviceEnumerator *en = NULL;
        CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en);
        LPWSTR defId = NULL; IMMDevice *def = NULL;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &def))) def->GetId(&defId);
        IMMDeviceCollection *col = NULL; en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col);
        UINT n = 0; col->GetCount(&n);
        for (UINT i = 0; i < n; i++) {
            IMMDevice *d = NULL; col->Item(i, &d);
            LPWSTR id = NULL; d->GetId(&id);
            IPropertyStore *ps = NULL; d->OpenPropertyStore(STGM_READ, &ps);
            PROPVARIANT pv; PropVariantInit(&pv); ps->GetValue(PKEY_Device_FriendlyName, &pv);
            wprintf(L"%s %s | %s\n", (defId && wcscmp(id, defId) == 0) ? L"[PADRAO]" : L"        ", id, pv.pwszVal);
            PropVariantClear(&pv); ps->Release(); CoTaskMemFree(id); d->Release();
        }
        return 0;
    }
    if (argc < 3) { fprintf(stderr, "uso: play_tone list | play_tone <id> <saida.wav> [segundos] [freq]\n"); return 2; }
    const wchar_t *needle = argv[1];
    const wchar_t *outPath = argv[2];
    double seconds = argc > 3 ? _wtof(argv[3]) : 3.0;
    double freq = argc > 4 ? _wtof(argv[4]) : 1000.0;

    CHECK(CoInitializeEx(NULL, COINIT_MULTITHREADED), "CoInitialize");
    IMMDeviceEnumerator *en = NULL;
    CHECK(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en), "enumerador");
    IMMDevice *dev = FindDevice(en, needle);
    if (!dev) { fprintf(stderr, "endpoint nao encontrado\n"); return 1; }
    IPropertyStore *ps = NULL; dev->OpenPropertyStore(STGM_READ, &ps);
    PROPVARIANT pv; PropVariantInit(&pv); ps->GetValue(PKEY_Device_FriendlyName, &pv);
    wprintf(L"Dispositivo: %s\n", pv.pwszVal); PropVariantClear(&pv); ps->Release();

    IAudioClient *rc = NULL;
    CHECK(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&rc), "Activate render");
    WAVEFORMATEX *rfmt = NULL; CHECK(rc->GetMixFormat(&rfmt), "GetMixFormat");
    wprintf(L"Mix: %lu Hz, %u canais, %u bits\n", rfmt->nSamplesPerSec, rfmt->nChannels, rfmt->wBitsPerSample);
    CHECK(rc->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, rfmt, NULL), "Initialize render");
    IAudioRenderClient *rr = NULL; CHECK(rc->GetService(__uuidof(IAudioRenderClient), (void **)&rr), "IAudioRenderClient");
    UINT32 rbuf = 0; rc->GetBufferSize(&rbuf);

    IAudioClient *cc = NULL;
    CHECK(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void **)&cc), "Activate capture");
    WAVEFORMATEX *cfmt = NULL; CHECK(cc->GetMixFormat(&cfmt), "GetMixFormat cap");
    CHECK(cc->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, cfmt, NULL), "Initialize loopback");
    IAudioCaptureClient *cap = NULL; CHECK(cc->GetService(__uuidof(IAudioCaptureClient), (void **)&cap), "IAudioCaptureClient");

    const int ch = rfmt->nChannels, rate = (int)rfmt->nSamplesPerSec;
    const int cch = cfmt->nChannels;
    std::vector<float> rec;
    double phase = 0, step = 2 * 3.141592653589793 * freq / rate;
    const float amp = 0.25f;

    BYTE *p = NULL; CHECK(rr->GetBuffer(rbuf, &p), "GetBuffer");
    float *fp = (float *)p;
    for (UINT32 i = 0; i < rbuf; i++) { float v = amp * (float)sin(phase); phase += step; for (int c = 0; c < ch; c++) fp[i * ch + c] = c < 2 ? v : 0.0f; }
    rr->ReleaseBuffer(rbuf, 0);

    CHECK(cc->Start(), "Start capture");
    CHECK(rc->Start(), "Start render");
    ULONGLONG t0 = GetTickCount64();
    while (GetTickCount64() - t0 < (ULONGLONG)(seconds * 1000)) {
        Sleep(5);
        UINT32 pad = 0; rc->GetCurrentPadding(&pad);
        UINT32 avail = rbuf - pad;
        if (avail > 0 && SUCCEEDED(rr->GetBuffer(avail, &p))) {
            fp = (float *)p;
            for (UINT32 i = 0; i < avail; i++) { float v = amp * (float)sin(phase); phase += step; for (int c = 0; c < ch; c++) fp[i * ch + c] = c < 2 ? v : 0.0f; }
            rr->ReleaseBuffer(avail, 0);
        }
        UINT32 pk = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&pk)) && pk > 0) {
            BYTE *cp = NULL; UINT32 nf = 0; DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&cp, &nf, &flags, NULL, NULL))) break;
            size_t off = rec.size(); rec.resize(off + (size_t)nf * cch);
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) memset(&rec[off], 0, (size_t)nf * cch * sizeof(float));
            else memcpy(&rec[off], cp, (size_t)nf * cch * sizeof(float));
            cap->ReleaseBuffer(nf);
        }
    }
    rc->Stop(); cc->Stop();

    FILE *o = _wfopen(outPath, L"wb");
    if (!o) { fprintf(stderr, "nao abriu saida\n"); return 1; }
    uint32_t dlen = (uint32_t)(rec.size() * 4);
    uint32_t v32; uint16_t v16;
    fwrite("RIFF", 1, 4, o); v32 = 36 + dlen; fwrite(&v32, 4, 1, o); fwrite("WAVE", 1, 4, o);
    fwrite("fmt ", 1, 4, o); v32 = 16; fwrite(&v32, 4, 1, o); v16 = 3; fwrite(&v16, 2, 1, o); v16 = (uint16_t)cch; fwrite(&v16, 2, 1, o);
    v32 = (uint32_t)cfmt->nSamplesPerSec; fwrite(&v32, 4, 1, o); v32 = (uint32_t)(cfmt->nSamplesPerSec * cch * 4); fwrite(&v32, 4, 1, o);
    v16 = (uint16_t)(cch * 4); fwrite(&v16, 2, 1, o); v16 = 32; fwrite(&v16, 2, 1, o);
    fwrite("data", 1, 4, o); fwrite(&dlen, 4, 1, o); fwrite(rec.data(), 4, rec.size(), o);
    fclose(o);
    wprintf(L"Gravado loopback: %zu frames em %s\n", rec.size() / cch, outPath);
    return 0;
}
