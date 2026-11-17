#include "musix_dsp.h"
#include <math.h>
#include <string.h>

static const short VC_TABLE[11] = {
    0, 16383, 16296, 16292, 16264, 16224, 16216, 16164, 16100, 16020, 15880
};

static int vc_sr_index(int rate) {
    if (rate > 48000) return 1;
    if (rate > 44100) return 2;
    if (rate > 32000) return 3;
    if (rate > 24000) return 4;
    if (rate > 22050) return 5;
    if (rate > 16000) return 6;
    if (rate > 12000) return 7;
    if (rate > 11025) return 8;
    if (rate > 8000)  return 9;
    return 10;
}

void musix_vc_init(musix_vc_t *vc) {
    memset(vc, 0, sizeof(*vc));
}

void musix_vc_reset(musix_vc_t *vc) {
    memset(vc->s, 0, sizeof(vc->s));
}

static void vc_recalc(musix_vc_t *vc) {
    if (vc->rate == 0) return;
    if (vc->sr_index < 1 || vc->sr_index > 10) return;
    vc->coef_q14 = vc->enabled ? VC_TABLE[vc->sr_index] : 0;
    vc->a = (float)vc->coef_q14 * 6.103515625e-05f;
    musix_vc_reset(vc);
}

int musix_vc_set_format(musix_vc_t *vc, int rate, int channels) {
    if (rate <= 0) return 0;
    if (((channels - 2) & ~6) != 0) return 0;
    vc->channels = channels;
    if (vc->rate != rate) {
        int idx;
        vc->rate = rate;
        idx = vc_sr_index(rate);
        if (vc->sr_index != idx) {
            vc->sr_index = idx;
            if (vc->enabled) vc_recalc(vc);
        }
    }
    return 1;
}

void musix_vc_set_enabled(musix_vc_t *vc, int enabled) {
    vc->enabled = enabled ? 1 : 0;
    vc_recalc(vc);
}

int musix_vc_is_active(const musix_vc_t *vc) {
    return vc->coef_q14 != 0;
}

static void vc_pair(musix_vc_t *vc, const float *in, float *out,
                    int frames, int ch, int pair) {
    const float half = 0.5f;
    const float a = vc->a;
    float s = vc->s[pair >> 1];
    int n;
    for (n = 0; n < frames; n++) {
        float L = in[n * ch + pair];
        float R = in[n * ch + pair + 1];
        float sum, m, d, t;
        sum = R + L;
        d   = L - R;
        m   = sum * half;
        t   = s - m;
        t   = t * a;
        s   = m + t;
        out[n * ch + pair]     = d + s;
        out[n * ch + pair + 1] = s - d;
    }
    vc->s[pair >> 1] = s;
}

static void copy_channel(const float *in, float *out, int frames, int ch, int c) {
    int n;
    if (in == out) return;
    for (n = 0; n < frames; n++) out[n * ch + c] = in[n * ch + c];
}

static void zero_channel(float *out, int frames, int ch, int c) {
    int n;
    for (n = 0; n < frames; n++) out[n * ch + c] = 0.0f;
}

void musix_vc_process(musix_vc_t *vc, const float *in, float *out, int frames) {
    int ch = vc->channels;
    if (vc->coef_q14 == 0 || ch == 0) {
        if (in != out && ch > 0) memcpy(out, in, (size_t)frames * ch * sizeof(float));
        return;
    }
    switch (ch) {
    case 2:
        vc_pair(vc, in, out, frames, ch, 0);
        break;
    case 4:
        vc_pair(vc, in, out, frames, ch, 0);
        if (vc->quad_center == 0) {
            vc_pair(vc, in, out, frames, ch, 2);
        } else {
            zero_channel(out, frames, ch, 2);
            copy_channel(in, out, frames, ch, 3);
        }
        break;
    case 6:
        vc_pair(vc, in, out, frames, ch, 0);
        vc_pair(vc, in, out, frames, ch, 4);
        zero_channel(out, frames, ch, 2);
        copy_channel(in, out, frames, ch, 3);
        break;
    case 8:
        vc_pair(vc, in, out, frames, ch, 0);
        vc_pair(vc, in, out, frames, ch, 4);
        vc_pair(vc, in, out, frames, ch, 6);
        zero_channel(out, frames, ch, 2);
        copy_channel(in, out, frames, ch, 3);
        break;
    default:
        if (in != out) memcpy(out, in, (size_t)frames * ch * sizeof(float));
        break;
    }
}

static const int PS_TABLE[9][3] = {
    { 13, -53248,  -832 },
    { 10, -40960,  -640 },
    {  7, -28672,  -448 },
    {  4, -16384,  -256 },
    {  0,      0,     0 },
    { -4,  16384,   256 },
    { -8,  32768,   512 },
    {-12,  49152,   768 },
    {-16,  65536,  1024 },
};

static float PS_WIN[MUSIX_PS_N + 1];
static int   PS_WIN_READY = 0;

const float *musix_ps_window(void) {
    if (!PS_WIN_READY) {
        const double pi    = 3.141592653589793;
        const double step  = 0.0001220703125;
        const double gain  = 0.70710678;
        int i;
        for (i = 0; i <= MUSIX_PS_N; i++) {
            double x = (double)i * pi * step;
            PS_WIN[i] = (float)(sin(x) * gain);
        }
        PS_WIN_READY = 1;
    }
    return PS_WIN;
}

void musix_ps_init(musix_ps_t *ps) {
    memset(ps, 0, sizeof(*ps));
    musix_ps_window();
}

void musix_ps_reset(musix_ps_t *ps) {
    memset(ps->buf, 0, sizeof(ps->buf));
}

static void ps_recalc(musix_ps_t *ps) {
    const int *t;
    if (ps->rate == 0) return;
    if (ps->semitone < -4 || ps->semitone > 4) return;
    t = PS_TABLE[ps->semitone + 4];
    ps->a = t[0];
    ps->b = t[1];
    ps->c = t[2];
    ps->st.k = 0;
    ps->st.D = (ps->a > 0) ? -ps->b : ps->b;
}

int musix_ps_set_format(musix_ps_t *ps, int rate, int channels) {
    if (rate <= 0) return 0;
    if (((channels - 2) & ~6) != 0) return 0;
    ps->channels = channels;
    if (ps->rate != rate) {
        ps->rate = rate;
        musix_ps_reset(ps);
        if (ps->semitone != 0) ps_recalc(ps);
    }
    return 1;
}

int musix_ps_set_semitone(musix_ps_t *ps, int semitone) {
    if (semitone > 4)  semitone = 4;
    if (semitone < -4) semitone = -4;
    if (ps->semitone != semitone) {
        ps->semitone = semitone;
        ps_recalc(ps);
    }
    return semitone;
}

int musix_ps_is_active(const musix_ps_t *ps) {
    return ps->a != 0;
}

static void ps_pair(musix_ps_t *ps, musix_ps_state_t *st,
                    const float *in, float *out, int frames, int ch, int pair) {
    const float *W = PS_WIN;
    float *bufL = ps->buf + (pair >> 1) * (2 * MUSIX_PS_N);
    float *bufR = bufL + MUSIX_PS_N;
    const float q6 = 0.015625f;
    const int a = ps->a, b = ps->b, c = ps->c;
    int pos = st->pos, D = st->D, k = st->k;
    int n;

    for (n = 0; n < frames; n++) {
        float xL = in[n * ch + pair];
        float xR = in[n * ch + pair + 1];
        int wi, di, i1, i2, i1m, i2m;
        unsigned frac_i;
        float frac, w2, w1;
        float L1, L2, R2, dL1, dL2, dR2, dR1, R1;
        float hL1, hL2, hR1, hR2, oL, oR;

        pos = (pos + 1) & (MUSIX_PS_N - 1);
        wi  = (pos + 0x4F8) & (MUSIX_PS_N - 1);
        bufL[wi] = xL;
        bufR[wi] = xR;

        frac_i = (unsigned)D & 0x3Fu;
        di     = (int)((unsigned)D >> 6);
        i1  = (wi - di) & (MUSIX_PS_N - 1);
        i2  = (wi - c - di) & (MUSIX_PS_N - 1);
        i1m = (wi - di - 1) & (MUSIX_PS_N - 1);
        i2m = (wi - c - di - 1) & (MUSIX_PS_N - 1);

        L1  = bufL[i1];
        L2  = bufL[i2];
        R2  = bufR[i2];
        dL1 = bufL[i1m] - L1;
        dL2 = bufL[i2m] - L2;
        dR2 = bufR[i2m] - R2;
        R1  = bufR[i1];
        dR1 = bufR[i1m] - R1;

        frac = (float)(int)frac_i * q6;
        w2 = W[k];
        w1 = W[MUSIX_PS_N - k];

        hL2 = dL2 * frac + L2;
        hR2 = dR2 * frac + R2;
        hL1 = dL1 * frac + L1;
        hR1 = dR1 * frac + R1;

        hL2 = hL2 * w2;
        hR2 = hR2 * w2;
        hL1 = hL1 * w1;
        hR1 = hR1 * w1;

        oL = hL2 + hL1;
        oR = hR2 + hR1;

        k += 1;
        D += (k >> 12) * b + a;
        k &= (MUSIX_PS_N - 1);

        out[n * ch + pair]     = oL;
        out[n * ch + pair + 1] = oR;
    }
    st->pos = pos; st->D = D; st->k = k;
}

void musix_ps_process(musix_ps_t *ps, const float *in, float *out, int frames) {
    int ch = ps->channels;
    musix_ps_state_t work;
    int pair;
    if (ps->a == 0 || ch == 0) {
        if (in != out && ch > 0) memcpy(out, in, (size_t)frames * ch * sizeof(float));
        return;
    }
    work = ps->st;
    for (pair = 0; pair + 1 < ch; pair += 2) {
        work = ps->st;
        ps_pair(ps, &work, in, out, frames, ch, pair);
    }
    ps->st = work;
}

void musix_chain_init(musix_chain_t *c) {
    musix_vc_init(&c->vc);
    musix_ps_init(&c->ps);
    c->order_ps_first = 0;
}

int musix_chain_set_format(musix_chain_t *c, int rate, int channels) {
    int ok1 = musix_vc_set_format(&c->vc, rate, channels);
    int ok2 = musix_ps_set_format(&c->ps, rate, channels);
    return ok1 && ok2;
}

void musix_chain_process(musix_chain_t *c, const float *in, float *out, int frames) {
    if (c->order_ps_first) {
        musix_ps_process(&c->ps, in, out, frames);
        musix_vc_process(&c->vc, out, out, frames);
    } else {
        musix_vc_process(&c->vc, in, out, frames);
        musix_ps_process(&c->ps, out, out, frames);
    }
}
