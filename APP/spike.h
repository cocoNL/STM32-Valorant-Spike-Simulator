#ifndef __SPIKE_H
#define __SPIKE_H
#include "sys.h"

/* Spike states */
typedef enum {
    STATE_UNDEPLOYED = 0,
    STATE_DEPLOYING,
    STATE_DEPLOYED,
    STATE_DEFUSING,
    STATE_DEFUSED,
    STATE_DETONATED
} spike_state_t;

/* LED blinking phase definition */
typedef struct {
    uint32_t duration_ms;   /* total duration of this phase */
    uint16_t on_ms;         /* LED on time */
    uint16_t off_ms;        /* LED off time */
} led_phase_t;

/* Spike state machine context */
typedef struct {
    spike_state_t state;

    /* countdown timing */
    uint32_t countdown_start_ms; /* tick when countdown started */
    uint32_t countdown_elapsed;  /* elapsed ms in deployed state */

    /* defuse timing */
    uint32_t defuse_press_ms;   /* tick when defuse press started (0=not pressing) */
    float    defuse_progress;    /* 0.0 ~ 1.0 */
    float    defuse_saved;       /* saved progress (0.0 or 0.5) */
    uint8_t  defuse_half_done;   /* 1=50% progress already saved */

    /* LED blinking */
    uint8_t  led_phase_idx;      /* current phase index 0-5 */
    uint32_t led_phase_elapsed;  /* elapsed ms within current phase */

    /* audio track management */
    uint8_t  main_audio_done;    /* main audio (defused/boom) finished */
    uint8_t  egg_playing;        /* easter egg currently playing */
    uint8_t  egg_text_switched;   /* 1=bottom text already showing switch hint */

    /* easter egg */
    uint8_t  egg_count;          /* number of files in current egg dir */
    uint8_t  egg_index;          /* current egg file index */
    char     egg_dir[64];        /* current egg directory path */
    char     egg_files[20][64];  /* egg file names (max 20 files) */
    char     egg_current[128];   /* current full path */

    /* deploying delayed stop */
    uint32_t deploy_stop_ms;     /* tick when to stop audio after early release */

    /* time display */
    float    display_time;       /* time to show on LCD (remaining or needed) */
    uint8_t  was_defusing;       /* 1=was defusing when detonated */
} spike_t;

extern spike_t spike;

/* Main API */
void spike_init(void);
void spike_loop(void);

/* Internal helpers */
void spike_led_update(void);
void spike_lcd_update(void);
void spike_audio_manage(void);
void spike_egg_load_dir(const char *dir);
void spike_egg_play_random(void);
void spike_egg_next(void);
void spike_state_transition(spike_state_t new_state);

#endif
