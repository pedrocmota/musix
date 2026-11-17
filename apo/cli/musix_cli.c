/*
 * musix_cli — aplica VoiceCancel/PitchShift a um WAV, para testes de nulo.
 *
 *   musix_cli entrada.wav saida.wav [--vc] [--pitch N] [--order ps-vc]
 *                                   [--block FRAMES]
 *
 * Entrada: PCM 16/24/32 bits ou float32, 2/4/6/8 canais. Saída: float32.
 * PCM é convertido como o motor do Windows faz (inteiro / 2^(bits-1)).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../dsp/musix_dsp.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr32(FILE *f, uint32_t v) { uint8_t b[4] = { v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255 }; fwrite(b, 1, 4, f); }
static void wr16(FILE *f, uint16_t v) { uint8_t b[2] = { v & 255, (v >> 8) & 255 }; fwrite(b, 1, 2, f); }

int main(int argc, char **argv)
{
    const char *inp = NULL, *outp = NULL;
    int vc = 0, pitch = 0, order = 0, block = 480;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--vc")) vc = 1;
        else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) pitch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--order") && i + 1 < argc) order = !strcmp(argv[++i], "ps-vc");
        else if (!strcmp(argv[i], "--block") && i + 1 < argc) block = atoi(argv[++i]);
        else if (!inp) inp = argv[i];
        else if (!outp) outp = argv[i];
        else { fprintf(stderr, "argumento inesperado: %s\n", argv[i]); return 2; }
    }
    if (!inp || !outp || block <= 0) {
        fprintf(stderr, "uso: musix_cli entrada.wav saida.wav [--vc] [--pitch N] [--order ps-vc] [--block N]\n");
        return 2;
    }

    FILE *f = fopen(inp, "rb");
    if (!f) { perror(inp); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc((size_t)sz);
    if (!data || fread(data, 1, (size_t)sz, f) != (size_t)sz) { fprintf(stderr, "falha ao ler\n"); return 1; }
    fclose(f);
    if (sz < 12 || memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) { fprintf(stderr, "nao e WAV\n"); return 1; }

    int fmt_tag = 0, channels = 0, rate = 0, bits = 0;
    const uint8_t *pcm = NULL; uint32_t pcm_len = 0;
    long pos = 12;
    while (pos + 8 <= sz) {
        const uint8_t *ck = data + pos; uint32_t len = rd32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            fmt_tag = rd16(ck + 8); channels = rd16(ck + 10); rate = (int)rd32(ck + 12); bits = rd16(ck + 22);
            if (fmt_tag == 0xFFFE && len >= 40) fmt_tag = rd16(ck + 8 + 24);  /* SubFormat: primeiros 2 bytes */
        } else if (!memcmp(ck, "data", 4)) {
            pcm = ck + 8; pcm_len = len; if ((long)pcm_len > sz - pos - 8) pcm_len = (uint32_t)(sz - pos - 8);
        }
        pos += 8 + len + (len & 1);
    }
    if (!pcm || channels <= 0) { fprintf(stderr, "WAV sem fmt/data\n"); return 1; }
    int is_float = (fmt_tag == 3);
    if (!is_float && fmt_tag != 1) { fprintf(stderr, "formato nao suportado (tag %d)\n", fmt_tag); return 1; }
    if (is_float && bits != 32) { fprintf(stderr, "float deve ter 32 bits\n"); return 1; }
    if (!is_float && bits != 16 && bits != 24 && bits != 32) { fprintf(stderr, "PCM deve ter 16/24/32 bits\n"); return 1; }

    int bps = bits / 8;
    uint32_t frames = pcm_len / (uint32_t)(bps * channels);
    float *buf = (float *)malloc((size_t)frames * channels * sizeof(float));
    uint32_t n;
    for (n = 0; n < frames * (uint32_t)channels; n++) {
        const uint8_t *p = pcm + (size_t)n * bps;
        if (is_float) { memcpy(&buf[n], p, 4); }
        else if (bps == 2) { int16_t v = (int16_t)rd16(p); buf[n] = (float)v * (1.0f / 32768.0f); }
        else if (bps == 3) { int32_t v = (int32_t)((p[0] << 8) | (p[1] << 16) | ((uint32_t)p[2] << 24)) >> 8; buf[n] = (float)v * (1.0f / 8388608.0f); }
        else { int32_t v = (int32_t)rd32(p); buf[n] = (float)v * (1.0f / 2147483648.0f); }
    }

    musix_chain_t *c = (musix_chain_t *)malloc(sizeof(musix_chain_t));
    musix_chain_init(c);
    if (!musix_chain_set_format(c, rate, channels)) {
        fprintf(stderr, "formato rejeitado pelo efeito (canais devem ser 2/4/6/8): copiando sem processar\n");
    }
    c->order_ps_first = order;
    musix_vc_set_enabled(&c->vc, vc);
    musix_ps_set_semitone(&c->ps, pitch);

    for (n = 0; n < frames; n += (uint32_t)block) {
        int k = (int)((frames - n) < (uint32_t)block ? (frames - n) : (uint32_t)block);
        musix_chain_process(c, buf + (size_t)n * channels, buf + (size_t)n * channels, k);
    }

    FILE *o = fopen(outp, "wb");
    if (!o) { perror(outp); return 1; }
    uint32_t dlen = frames * channels * 4;
    fwrite("RIFF", 1, 4, o); wr32(o, 36 + dlen); fwrite("WAVE", 1, 4, o);
    fwrite("fmt ", 1, 4, o); wr32(o, 16); wr16(o, 3); wr16(o, (uint16_t)channels); wr32(o, (uint32_t)rate);
    wr32(o, (uint32_t)(rate * channels * 4)); wr16(o, (uint16_t)(channels * 4)); wr16(o, 32);
    fwrite("data", 1, 4, o); wr32(o, dlen);
    fwrite(buf, 4, (size_t)frames * channels, o);
    fclose(o);

    fprintf(stderr, "ok: %u frames, %d Hz, %d ch, vc=%d pitch=%d order=%s\n",
            frames, rate, channels, vc, pitch, order ? "ps-vc" : "vc-ps");
    free(buf); free(c); free(data);
    return 0;
}
