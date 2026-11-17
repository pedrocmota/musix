/*
 * Musix — núcleo DSP do efeito "Karaoke" (VoiceCancel + PitchShift).
 *
 * Transcrição bit-exata das classes VoiceSuppress e PitchShift de
 * RltkAPOU642.dll (Realtek UAPO2). Toda a aritmética é float32 com uma
 * única arredondação por operação, na mesma ordem do binário original.
 * Compilar sem FMA e sem x87: MSVC x64 com /fp:precise /fp:contract-.
 *
 * Áudio intercalado (frame = canais consecutivos). in e out podem ser o
 * mesmo buffer.
 */
#ifndef MUSIX_DSP_H
#define MUSIX_DSP_H

#ifdef __cplusplus
extern "C" {
#endif

#define MUSIX_PS_N        4096           /* buffer circular / período do crossfade */
#define MUSIX_MAX_PAIRS   4              /* até 8 canais */

/* ------------------------------------------------------------------ */
/* VoiceCancel (classe VoiceSuppress)                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    int   enabled;        /* pedido pelo usuário */
    int   channels;       /* 2, 4, 6 ou 8; 0 = sem formato */
    int   rate;           /* Hz */
    int   sr_index;       /* 1..10, faixa de taxa de amostragem */
    int   coef_q14;       /* 0 = desligado (bypass); senão tabela[sr_index] */
    float a;              /* coef_q14 * (1/16384) */
    float s[MUSIX_MAX_PAIRS]; /* estado do passa-baixa, um por par */
    int   quad_center;    /* 4 canais: !=0 trata (2,3) como centro/LFE */
} musix_vc_t;

void musix_vc_init(musix_vc_t *vc);
/* Aceita só channels em {2,4,6,8} e rate > 0; devolve 1 se aceito. */
int  musix_vc_set_format(musix_vc_t *vc, int rate, int channels);
void musix_vc_set_enabled(musix_vc_t *vc, int enabled);
int  musix_vc_is_active(const musix_vc_t *vc);
void musix_vc_reset(musix_vc_t *vc);
void musix_vc_process(musix_vc_t *vc, const float *in, float *out, int frames);

/* ------------------------------------------------------------------ */
/* PitchShift (seletor "Karaoke -4..+4")                               */
/* ------------------------------------------------------------------ */
typedef struct {
    int pos;              /* índice de escrita (0..4095) */
    int D;                /* atraso de leitura em Q6 (1/64 amostra) */
    int k;                /* fase do crossfade (0..4095) */
} musix_ps_state_t;

typedef struct {
    int   semitone;       /* -4..4 */
    int   a, b, c;        /* parâmetros da tabela */
    int   channels;
    int   rate;
    musix_ps_state_t st;  /* estado compartilhado entre os pares */
    float buf[MUSIX_MAX_PAIRS * 2 * MUSIX_PS_N]; /* [par][L|R][4096] */
} musix_ps_t;

void musix_ps_init(musix_ps_t *ps);
int  musix_ps_set_format(musix_ps_t *ps, int rate, int channels);
/* Limita a -4..4; devolve o valor efetivo. */
int  musix_ps_set_semitone(musix_ps_t *ps, int semitone);
int  musix_ps_is_active(const musix_ps_t *ps);
void musix_ps_reset(musix_ps_t *ps);      /* zera só o buffer (como o original) */
void musix_ps_process(musix_ps_t *ps, const float *in, float *out, int frames);

/* Janela de crossfade (4097 entradas), gerada na primeira chamada. */
const float *musix_ps_window(void);

/* ------------------------------------------------------------------ */
/* Cadeia completa: VoiceCancel -> PitchShift                          */
/* ------------------------------------------------------------------ */
typedef struct {
    musix_vc_t vc;
    musix_ps_t ps;
    int order_ps_first;   /* 0 = VC depois PS (padrão); 1 = inverso */
} musix_chain_t;

void musix_chain_init(musix_chain_t *c);
int  musix_chain_set_format(musix_chain_t *c, int rate, int channels);
void musix_chain_process(musix_chain_t *c, const float *in, float *out, int frames);

#ifdef __cplusplus
}
#endif
#endif
