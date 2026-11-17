#ifndef MUSIX_DSP_H
#define MUSIX_DSP_H

#ifdef __cplusplus
extern "C" {
#endif

#define MUSIX_PS_N        4096
#define MUSIX_MAX_PAIRS   4

typedef struct {
    int   enabled;
    int   channels;
    int   rate;
    int   sr_index;
    int   coef_q14;
    float a;
    float s[MUSIX_MAX_PAIRS];
    int   quad_center;
} musix_vc_t;

void musix_vc_init(musix_vc_t *vc);
int  musix_vc_set_format(musix_vc_t *vc, int rate, int channels);
void musix_vc_set_enabled(musix_vc_t *vc, int enabled);
int  musix_vc_is_active(const musix_vc_t *vc);
void musix_vc_reset(musix_vc_t *vc);
void musix_vc_process(musix_vc_t *vc, const float *in, float *out, int frames);

typedef struct {
    int pos;
    int D;
    int k;
} musix_ps_state_t;

typedef struct {
    int   semitone;
    int   a, b, c;
    int   channels;
    int   rate;
    musix_ps_state_t st;
    float buf[MUSIX_MAX_PAIRS * 2 * MUSIX_PS_N];
} musix_ps_t;

void musix_ps_init(musix_ps_t *ps);
int  musix_ps_set_format(musix_ps_t *ps, int rate, int channels);
int  musix_ps_set_semitone(musix_ps_t *ps, int semitone);
int  musix_ps_is_active(const musix_ps_t *ps);
void musix_ps_reset(musix_ps_t *ps);
void musix_ps_process(musix_ps_t *ps, const float *in, float *out, int frames);

const float *musix_ps_window(void);

typedef struct {
    musix_vc_t vc;
    musix_ps_t ps;
    int order_ps_first;
} musix_chain_t;

void musix_chain_init(musix_chain_t *c);
int  musix_chain_set_format(musix_chain_t *c, int rate, int channels);
void musix_chain_process(musix_chain_t *c, const float *in, float *out, int frames);

#ifdef __cplusplus
}
#endif
#endif
