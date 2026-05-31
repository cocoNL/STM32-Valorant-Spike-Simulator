#include "spike.h"
#include "spike_audio.h"
#include "pcm_data.h"
#include "vs10xx.h"
#include "led.h"
#include "key.h"
#include "lcd.h"
#include "text.h"
#include "delay.h"
#include "malloc.h"
#include "ff.h"
#include "exfuns.h"
#include "stdio.h"
#include "string.h"

spike_t spike;

/* Use HAL_GetTick() for all timing - returns ms since boot */

/* LED blinking phases: {duration_ms, on_ms, off_ms} */
static const led_phase_t led_phases[6] = {
    {25000, 50, 950},   /* 0-25s:  ~1Hz  */
    {10000, 50, 450},   /* 25-35s: ~2Hz  */
    { 5000, 50, 200},   /* 35-40s: ~4Hz  */
    { 2500, 50,  75},   /* 40-42.5s: ~8Hz */
    { 1500, 25,  25},   /* 42.5-44s: ~20Hz */
    { 1000, 12,  13}    /* 44-45s: ~40Hz */
};

/* Progress bar geometry */
#define BAR_X     40
#define BAR_Y     80
#define BAR_W     400
#define BAR_H     30
#define BAR_MID   (BAR_X + BAR_W / 2)

/* Colors */
#define COLOR_BG        WHITE
#define COLOR_TEXT      BLACK
#define COLOR_BAR_BG    GRAY
#define COLOR_BAR_FILL  BLUE
#define COLOR_BAR_LINE  BLACK
#define COLOR_TIME      RED

/* Forward declarations */
static void spike_enter_state(spike_state_t new_state);
static void spike_draw_progress_bar(float progress);
static void spike_lcd_clear_area(uint16_t y, uint16_t h);
static void spike_update_display_time(void);
static uint32_t key_up_press_duration(void);
static uint8_t key0_pressed(void);

/*──────────────────────────────────────────────────────────────
 * Initialization
 *──────────────────────────────────────────────────────────────*/
void spike_init(void)
{
    memset(&spike, 0, sizeof(spike_t));
    spike.state = STATE_UNDEPLOYED;

    /* Turn off both LEDs */
    LED0 = 1;  /* PB5 red LED off (active low) */
    LED1 = 1;  /* PE5 green LED off (active low) */

    /* Clear LCD */
    LCD_Clear(COLOR_BG);

    /* Initialize audio subsystem */
    spike_audio_init();

    /* Enable VS1053 LINE_IN monitor */
    vs_linein_monitor_enable();
}

/*──────────────────────────────────────────────────────────────
 * Main loop - called from main()
 *──────────────────────────────────────────────────────────────*/
void spike_loop(void)
{
    /* Always feed audio data */
    spike_audio_feed();

    /* Update LED based on current state */
    spike_led_update();

    /* Handle state-specific logic */
    switch (spike.state) {
    case STATE_UNDEPLOYED:
        if (key_up_press_duration() > 0) {
            spike_enter_state(STATE_DEPLOYING);
        }
        break;

    case STATE_DEPLOYING: {
        uint32_t hold = key_up_press_duration();
        if (hold == 0) {
            /* Released before 4s */
            spike_audio_stop();
            spike_enter_state(STATE_UNDEPLOYED);
        } else if (hold >= 4000) {
            spike_audio_stop();
            spike_enter_state(STATE_DEPLOYED);
        }
        break;
    }

    case STATE_DEPLOYED: {
        /* Update elapsed time */
        spike.countdown_elapsed = HAL_GetTick() - spike.countdown_start_ms;

        /* Check for defuse start */
        if (key_up_press_duration() > 0) {
            spike_enter_state(STATE_DEFUSING);
            break;
        }

        /* Check for detonation */
        if (spike.countdown_elapsed >= 45000) {
            spike.was_defusing = 0;
            spike_enter_state(STATE_DETONATED);
        }
        break;
    }

    case STATE_DEFUSING: {
        uint32_t hold = key_up_press_duration();
        uint32_t defuse_start = spike.defuse_press_ms;

        /* Calculate current defuse progress */
        if (hold > 0) {
            uint32_t effective_start = spike.defuse_half_done ? 3500 : 0;
            float current_hold_s = (float)(HAL_GetTick() - defuse_start) / 1000.0f;
            float total_needed = spike.defuse_half_done ? 3.5f : 7.0f;
            spike.defuse_progress = spike.defuse_saved +
                (current_hold_s / total_needed) * (1.0f - spike.defuse_saved);
            if (spike.defuse_progress > 1.0f) spike.defuse_progress = 1.0f;
        }

        /* Update countdown */
        spike.countdown_elapsed = HAL_GetTick() - spike.countdown_start_ms;

        if (hold == 0 && defuse_start > 0) {
            /* Button was released */
            uint32_t press_dur = HAL_GetTick() - defuse_start;

            if (press_dur < 3500) {
                /* Less than 3.5s: no progress saved */
                spike.defuse_progress = 0.0f;
                spike.defuse_saved = 0.0f;
                spike.defuse_half_done = 0;
                pcm_stop();
                LED1 = 1;  /* green LED off */
                spike_enter_state(STATE_DEPLOYED);
            } else if (press_dur >= 3500 && press_dur < 7000) {
                /* 3.5s to 7s: save 50% progress */
                spike.defuse_saved = 0.5f;
                spike.defuse_progress = 0.5f;
                spike.defuse_half_done = 1;
                pcm_stop();
                LED1 = 1;  /* green LED off */
                spike_enter_state(STATE_DEPLOYED);
            } else if (press_dur >= 7000) {
                /* 7s+: defused! */
                spike.defuse_progress = 1.0f;
                spike_audio_stop();
                pcm_stop();
                spike_enter_state(STATE_DEFUSED);
            }

            /* Clear defuse press tracking */
            spike.defuse_press_ms = 0;
            spike.was_defusing = 0;
            break;
        }

        /* Check if detonated while defusing */
        if (spike.countdown_elapsed >= 45000) {
            spike.was_defusing = 1;
            /* Calculate remaining defuse time needed */
            spike.display_time = 7.0f - spike.defuse_progress * 7.0f;
            spike_audio_stop();
            pcm_stop();
            LED1 = 1;  /* green LED off */
            spike_enter_state(STATE_DETONATED);
        }
        break;
    }

    case STATE_DEFUSED:
        /* After defused.mp3 finishes, start easter egg */
        if (!spike_audio_is_busy() && !spike.egg_playing) {
            spike.main_audio_done = 1;
            spike_egg_play_random();
            spike.egg_playing = 1;
        }
        /* KEY0 to switch easter egg */
        if (key0_pressed() && spike.egg_playing) {
            spike_egg_next();
        }
        break;

    case STATE_DETONATED:
        /* After boom.mp3 finishes, start easter egg */
        if (!spike_audio_is_busy() && !spike.egg_playing && spike.main_audio_done == 0) {
            spike.main_audio_done = 1;
            spike_egg_play_random();
            spike.egg_playing = 1;
        }
        /* KEY0 to switch easter egg */
        if (key0_pressed() && spike.egg_playing) {
            spike_egg_next();
        }
        break;
    }

    /* Update LCD display */
    spike_lcd_update();
}

/*──────────────────────────────────────────────────────────────
 * State transition handler
 *──────────────────────────────────────────────────────────────*/
static void spike_enter_state(spike_state_t new_state)
{
    spike_state_t old = spike.state;
    spike.state = new_state;

    switch (new_state) {
    case STATE_UNDEPLOYED:
        LED0 = 1;   /* red off */
        LED1 = 1;   /* green off */
        pcm_stop();
        LCD_Clear(COLOR_BG);
        spike.deploy_press_ms = 0;
        spike.defuse_press_ms = 0;
        spike.defuse_progress = 0.0f;
        spike.defuse_saved = 0.0f;
        spike.defuse_half_done = 0;
        spike.main_audio_done = 0;
        spike.egg_playing = 0;
        spike.was_defusing = 0;
        break;

    case STATE_DEPLOYING:
        spike.deploy_press_ms = HAL_GetTick();
        spike.defuse_progress = 0.0f;
        spike.defuse_saved = 0.0f;
        spike.defuse_half_done = 0;
        spike_audio_play_start("0:/SOUNDS/planting.mp3");
        /* LCD shows planting text */
        LCD_Clear(COLOR_BG);
        Show_Str(BAR_X, 100, 400, 24, (uint8_t *)"正在部署", 24, 0);
        Show_Str(BAR_X, 130, 400, 24, (uint8_t *)"SPIKE PLANTING", 24, 0);
        break;

    case STATE_DEPLOYED:
        spike.countdown_start_ms = HAL_GetTick();
        spike.countdown_elapsed = 0;
        spike.led_phase_idx = 0;
        spike.led_phase_elapsed = 0;
        spike.led_last_toggle = HAL_GetTick();
        spike.led_on = 0;
        spike.defuse_half_done = 0;
        spike.defuse_saved = 0.0f;
        spike.defuse_progress = 0.0f;
        LED0 = 1;   /* red off initially */
        LED1 = 1;   /* green off */
        spike_audio_play_start("0:/SOUNDS/planted.mp3");
        /* LCD: progress bar + text */
        LCD_Clear(COLOR_BG);
        spike_draw_progress_bar(0.0f);
        Show_Str(BAR_X, 120, 400, 24, (uint8_t *)"爆能器已部署", 24, 0);
        Show_Str(BAR_X, 150, 400, 24, (uint8_t *)"SPIKE PLANTED", 24, 0);
        break;

    case STATE_DEFUSING: {
        spike.defuse_press_ms = HAL_GetTick();
        LED1 = 0;  /* green LED on */
        /* Play PCM defuse start sound */
        if (spike.defuse_half_done) {
            pcm_play_start(pcm_defuse_start_2, pcm_defuse_start_2_len);
        } else {
            pcm_play_start(pcm_defuse_start_1, pcm_defuse_start_1_len);
        }
        LCD_Clear(COLOR_BG);
        spike_draw_progress_bar(spike.defuse_saved);
        Show_Str(BAR_X, 120, 400, 24, (uint8_t *)"正在拆除", 24, 0);
        Show_Str(BAR_X, 150, 400, 24, (uint8_t *)"DEFUSING", 24, 0);
        break;
    }

    case STATE_DEFUSED:
        LED0 = 1;   /* red LED off */
        LED1 = 0;   /* green LED on */
        pcm_stop();
        /* Calculate remaining time */
        spike.display_time = 45.0f - (float)(HAL_GetTick() - spike.countdown_start_ms) / 1000.0f;
        if (spike.display_time < 0.0f) spike.display_time = 0.0f;
        spike.main_audio_done = 0;
        spike.egg_playing = 0;
        spike_audio_play_start("0:/SOUNDS/defused.mp3");
        /* LCD */
        LCD_Clear(COLOR_BG);
        spike_update_display_time();
        Show_Str(BAR_X, 150, 400, 24, (uint8_t *)"爆能器已拆除", 24, 0);
        Show_Str(BAR_X, 180, 400, 24, (uint8_t *)"SPIKE DEFUSED", 24, 0);
        Show_Str(BAR_X, 210, 400, 24, (uint8_t *)"按KEY0切换彩蛋", 24, 0);
        break;

    case STATE_DETONATED:
        LED0 = 0;   /* red LED on (constant) */
        LED1 = 1;   /* green LED off */
        pcm_stop();
        spike.main_audio_done = 0;
        spike.egg_playing = 0;
        spike_audio_play_start("0:/SOUNDS/boom.mp3");
        /* Load detonated egg directory */
        spike_egg_load_dir("0:/SOUNDS/Easter_eggs/detonated");
        /* LCD */
        LCD_Clear(COLOR_BG);
        if (spike.was_defusing) {
            spike_update_display_time();
        }
        Show_Str(BAR_X, 130, 400, 24, (uint8_t *)"爆能器已启动", 24, 0);
        Show_Str(BAR_X, 160, 400, 24, (uint8_t *)"SPIKE DETONATED", 24, 0);
        Show_Str(BAR_X, 190, 400, 24, (uint8_t *)"按KEY0切换彩蛋", 24, 0);
        break;
    }
}

/*──────────────────────────────────────────────────────────────
 * LED blinking update - called every loop iteration
 *──────────────────────────────────────────────────────────────*/
void spike_led_update(void)
{
    if (spike.state == STATE_DEPLOYED || spike.state == STATE_DEFUSING) {
        uint32_t elapsed = spike.countdown_elapsed;
        uint32_t phase_start = 0;
        uint8_t i;

        /* Find current phase */
        for (i = 0; i < 6; i++) {
            if (elapsed < phase_start + led_phases[i].duration_ms) {
                spike.led_phase_idx = i;
                spike.led_phase_elapsed = elapsed - phase_start;
                break;
            }
            phase_start += led_phases[i].duration_ms;
        }
        /* Beyond last phase: keep last phase behavior */
        if (i >= 6) {
            spike.led_phase_idx = 5;
            spike.led_phase_elapsed = elapsed - 44500;
        }

        /* Toggle LED based on phase timing */
        const led_phase_t *ph = &led_phases[spike.led_phase_idx];
        uint32_t phase_elapsed = spike.led_phase_elapsed;
        uint32_t cycle = ph->on_ms + ph->off_ms;
        uint32_t in_cycle = phase_elapsed % cycle;

        if (in_cycle < ph->on_ms) {
            LED0 = 0;  /* red LED on */
        } else {
            LED0 = 1;  /* red LED off */
        }
    } else if (spike.state == STATE_DETONATED) {
        LED0 = 0;  /* red LED on constant */
    } else if (spike.state != STATE_DEFUSED) {
        LED0 = 1;  /* red LED off */
    }
    /* STATE_DEFUSED: LED stays as set in enter_state */
}

/*──────────────────────────────────────────────────────────────
 * LCD update
 *──────────────────────────────────────────────────────────────*/
void spike_lcd_update(void)
{
    char buf[32];

    switch (spike.state) {
    case STATE_DEPLOYED:
        spike_draw_progress_bar(0.0f);
        break;

    case STATE_DEFUSING:
        spike_draw_progress_bar(spike.defuse_progress);
        break;

    case STATE_DEFUSED:
        /* Update time display */
        spike_update_display_time();
        /* Show audio status */
        if (!spike_audio_is_busy() && spike.main_audio_done) {
            spike.main_audio_done = 1;
            spike_lcd_clear_area(120, 30);
        }
        break;

    case STATE_DETONATED:
        if (spike.was_defusing) {
            spike_update_display_time();
        }
        if (!spike_audio_is_busy() && !spike.main_audio_done) {
            spike.main_audio_done = 1;
        }
        break;

    default:
        break;
    }
}

/* Draw progress bar with fill and midline */
static void spike_draw_progress_bar(float progress)
{
    uint16_t fill_w, i;

    /* Draw box outline */
    LCD_DrawRectangle(BAR_X - 1, BAR_Y - 1, BAR_X + BAR_W + 1, BAR_Y + BAR_H + 1);

    /* Erase interior */
    LCD_Fill(BAR_X, BAR_Y, BAR_X + BAR_W, BAR_Y + BAR_H, COLOR_BG);

    /* Draw filled portion */
    fill_w = (uint16_t)(progress * (float)BAR_W);
    if (fill_w > 0) {
        LCD_Fill(BAR_X, BAR_Y + 2, BAR_X + fill_w, BAR_Y + BAR_H - 2, COLOR_BAR_FILL);
    }

    /* Draw midline at 50% */
    LCD_DrawLine(BAR_MID, BAR_Y + 2, BAR_MID, BAR_Y + BAR_H - 2);
}

/* Clear a horizontal strip on LCD */
static void spike_lcd_clear_area(uint16_t y, uint16_t h)
{
    LCD_Fill(0, y, 480, y + h, COLOR_BG);
}

/* Update the xx.xx time display */
static void spike_update_display_time(void)
{
    char buf[32];
    sprintf(buf, "%.2f", spike.display_time);
    spike_lcd_clear_area(BAR_X, 40);
    Show_Str(BAR_X, 100, 200, 24, (uint8_t *)buf, 24, 0);
}

/*──────────────────────────────────────────────────────────────
 * Button helpers
 *──────────────────────────────────────────────────────────────*/

/* Get KEY_UP (PA0) continuous press duration in ms */
static uint32_t key_up_press_duration(void)
{
    static uint32_t press_start = 0;
    static uint8_t was_pressed = 0;
    uint8_t pressed = (WK_UP == 1);  /* PA0: pull-down, high when pressed */

    if (pressed && !was_pressed) {
        /* Just pressed */
        press_start = HAL_GetTick();
        was_pressed = 1;
    } else if (!pressed && was_pressed) {
        /* Just released */
        press_start = 0;
        was_pressed = 0;
        return 0;
    } else if (!pressed) {
        return 0;
    }

    /* Still pressed */
    if (press_start > 0) {
        uint32_t dur = HAL_GetTick() - press_start;
        if (dur > 10000) dur = 10000;  /* clamp */
        return dur;
    }
    return 0;
}

/* Debounced KEY0 press detection (one-shot) */
static uint8_t key0_pressed(void)
{
    static uint8_t last = 1;
    static uint32_t last_tick = 0;
    uint8_t cur = (KEY0 == 0) ? 1 : 0;  /* PE4: pull-up, low when pressed */

    if (cur != last && (HAL_GetTick() - last_tick) > 50) {
        last = cur;
        last_tick = HAL_GetTick();
        if (cur) return 1;
    }
    last = cur;
    return 0;
}

/*──────────────────────────────────────────────────────────────
 * Easter egg management
 *──────────────────────────────────────────────────────────────*/

/* Load list of MP3 files from a directory on SD card */
void spike_egg_load_dir(const char *dir)
{
    DIR mp3dir;
    FILINFO fno;
    FRESULT res;
    uint8_t i = 0;

    strcpy(spike.egg_dir, dir);
    spike.egg_count = 0;
    spike.egg_index = 0;

    res = f_opendir(&mp3dir, (const TCHAR *)dir);
    if (res != FR_OK) return;

    while (i < 20) {
        res = f_readdir(&mp3dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;

        /* Check if it's an MP3 file */
        int flen = strlen(fno.fname);
        if (flen > 4 && fno.fname[flen-4] == '.'
            && (fno.fname[flen-3] == 'm' || fno.fname[flen-3] == 'M')
            && (fno.fname[flen-2] == 'p' || fno.fname[flen-2] == 'P')
            && fno.fname[flen-1] == '3') {
            strcpy(spike.egg_files[i], fno.fname);
            i++;
        }
    }
    spike.egg_count = i;
    f_closedir(&mp3dir);
}

/* Play a random Easter egg from the loaded directory */
void spike_egg_play_random(void)
{
    if (spike.egg_count == 0) return;

    spike.egg_index = ((uint32_t)HAL_GetTick() / 100) % spike.egg_count;
    sprintf(spike.egg_current, "%s/%s", spike.egg_dir, spike.egg_files[spike.egg_index]);
    spike_audio_play_start(spike.egg_current);
}

/* Switch to next Easter egg */
void spike_egg_next(void)
{
    if (spike.egg_count == 0) return;

    spike.egg_index = (spike.egg_index + 1) % spike.egg_count;
    sprintf(spike.egg_current, "%s/%s", spike.egg_dir, spike.egg_files[spike.egg_index]);
    spike_audio_stop();
    while (spike_audio_is_busy()) {
        spike_audio_feed();
    }
    spike_audio_play_start(spike.egg_current);
}
