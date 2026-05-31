#ifndef __SPIKE_AUDIO_H
#define __SPIKE_AUDIO_H
#include "sys.h"
#include "ff.h"

/* VS1053 non-blocking audio player */
typedef struct {
    FIL     file;           /* FATFS file handle */
    uint8_t playing;        /* 1=playing, 0=idle */
    uint8_t done;           /* 1=song finished normally */
    uint8_t stop_req;       /* 1=stop was requested */
    uint8_t *buf;           /* 4096-byte read buffer from SRAM */
    uint16_t buf_pos;       /* current position in buffer */
    uint16_t buf_len;       /* valid bytes in buffer */
    uint8_t resume_needed;  /* 1=need time-synced resume after short */
} spike_audio_t;

/* PCM player (PWM audio via PA8) */
typedef struct {
    const uint8_t *data;    /* PCM sample data */
    uint32_t len;           /* total samples */
    uint32_t pos;           /* current sample index */
    uint8_t playing;        /* 1=playing */
} pcm_player_t;

extern spike_audio_t audio_player;
extern pcm_player_t pcm_player;

/* VS1053 audio functions */
void  spike_audio_init(void);
void  spike_audio_play_start(const char *path);
void  spike_audio_stop(void);
void  spike_audio_feed(void);
uint8_t spike_audio_is_busy(void);
void spike_audio_pause_and_play(const char *short_path);  /* pause current, play short */
void spike_audio_resume_if_needed(uint32_t elapsed_ms);  /* resume at time-synced offset */

/* PCM/PWM audio functions */
void pcm_timer_init(void);      /* TIM2 at PCM_SAMPLE_RATE for PWM audio */
void pcm_play_start(const uint8_t *data, uint32_t len);
void pcm_stop(void);

/* VS1053 LINE_IN monitor enable */
void vs_linein_monitor_enable(void);

#endif
