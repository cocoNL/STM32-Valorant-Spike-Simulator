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

static const led_phase_t led_phases[6] = {
    {25000, 50, 950},
    {10000, 50, 450},
    { 5000, 50, 200},
    { 2500, 50,  75},
    { 1500, 25,  25},
    { 1000, 12,  13}
};

#define BAR_X     40
#define BAR_Y     80
#define BAR_W     400
#define BAR_H     30
#define BAR_MID   (BAR_X + BAR_W / 2)

#define COLOR_BG        WHITE
#define COLOR_BAR_FILL  RED

static void spike_enter_state(spike_state_t new_state);
static void spike_draw_progress_bar(float progress, uint8_t show_midline);
static void spike_lcd_clear_area(uint16_t y, uint16_t h);
static void spike_update_display_time(const char *prefix);
static uint32_t key_up_hold_ms(void);
static uint32_t key1_hold_ms(void);
static uint8_t key0_released(void);

void spike_init(void)
{
    memset(&spike, 0, sizeof(spike_t));
    spike.state = STATE_UNDEPLOYED;
    LED0 = 1; LED1 = 1;
    LCD_Clear(COLOR_BG);
    Show_Str(BAR_X, 100, 400, 24, (uint8_t *)"\xB0\xB4KEY_UP\xB2\xBF\xCA\xF0\xB1\xAC\xC4\xDC\xC6\xF7", 24, 0);
    Show_Str(BAR_X, 130, 400, 24, (uint8_t *)"PRESS KEY_UP TO PLANT", 24, 0);
}

void spike_loop(void)
{
    uint8_t key;
    uint32_t hold;

    spike_audio_feed();
    spike_audio_resume_if_needed(spike.countdown_elapsed);

    key = KEY_Scan(1);
    hold = key_up_hold_ms();
    {
        uint32_t hold1 = key1_hold_ms();
        if (spike.state == STATE_DEFUSING) hold = hold1;
    }

    spike_led_update();

    switch (spike.state) {

    case STATE_UNDEPLOYED:
        if (key == WKUP_PRES) {
            spike_enter_state(STATE_DEPLOYING);
        }
        break;

    case STATE_DEPLOYING:
        if (hold == 0) {
            /* Released before 4s */
            spike_audio_stop();
            spike_enter_state(STATE_UNDEPLOYED);
        } else if (hold >= 4000) {
            spike_audio_stop();
            spike_enter_state(STATE_DEPLOYED);
        }
        break;

    case STATE_DEPLOYED:
        spike.countdown_elapsed = HAL_GetTick() - spike.countdown_start_ms;

        if (key == KEY1_PRES) {
            spike_enter_state(STATE_DEFUSING);
            break;
        }

        if (spike.countdown_elapsed >= 45000) {
            spike.was_defusing = 0;
            spike_enter_state(STATE_DETONATED);
        }
        break;

    case STATE_DEFUSING: {
        spike.countdown_elapsed = HAL_GetTick() - spike.countdown_start_ms;

        /* Update defuse progress */
        if (hold > 0) {
            float held_s = (float)hold / 1000.0f;
            float needed = spike.defuse_half_done ? 3.5f : 7.0f;
            spike.defuse_progress = spike.defuse_saved + (held_s / needed) * (1.0f - spike.defuse_saved);
            if (spike.defuse_progress > 1.0f) spike.defuse_progress = 1.0f;
        }

        /* Immediately succeed when progress reaches 100% */
        if (spike.defuse_progress >= 1.0f) {
            spike_audio_stop();
            pcm_stop();
            spike_enter_state(STATE_DEFUSED);
            break;
        }

        if (hold == 0 && spike.defuse_press_ms > 0) {
            /* Released */
            uint32_t dur = HAL_GetTick() - spike.defuse_press_ms;
            spike.defuse_press_ms = 0;

            if (spike.defuse_half_done) {
                /* Already saved 50%, need 3.5s more to complete */
                if (dur >= 3500) {
                    spike.defuse_progress = 1.0f;
                    spike_audio_stop();
                    pcm_stop();
                    spike_enter_state(STATE_DEFUSED);
                } else {
                    /* Keep 50% saved, progress stays at 0.5 */
                    spike.defuse_progress = 0.5f;
                    pcm_stop();
                    LED1 = 1;
                    spike_enter_state(STATE_DEPLOYED);
                }
            } else {
                /* Fresh defuse: need 7s total, 3.5s for half */
                if (dur < 3500) {
                    spike.defuse_progress = 0.0f;
                    spike.defuse_saved = 0.0f;
                    spike.defuse_half_done = 0;
                    pcm_stop();
                    LED1 = 1;
                    spike_enter_state(STATE_DEPLOYED);
                } else if (dur < 7000) {
                    spike.defuse_saved = 0.5f;
                    spike.defuse_progress = 0.5f;
                    spike.defuse_half_done = 1;
                    pcm_stop();
                    LED1 = 1;
                    spike_enter_state(STATE_DEPLOYED);
                } else {
                    spike.defuse_progress = 1.0f;
                    spike_audio_stop();
                    pcm_stop();
                    spike_enter_state(STATE_DEFUSED);
                }
            }
            break;
        }

        if (spike.countdown_elapsed >= 45000) {
            spike.was_defusing = 1;
            spike.display_time = 7.0f - spike.defuse_progress * 7.0f;
            spike_audio_stop();
            pcm_stop();
            LED1 = 1;
            spike_enter_state(STATE_DETONATED);
        }
        break;
    }

    case STATE_DEFUSED: {
        uint8_t k0 = key0_released();
        if (!spike_audio_is_busy() && !spike.main_audio_done) {
            spike.main_audio_done = 1;
        }
        if (k0 && spike.main_audio_done) {
            if (!spike.egg_playing) {
                spike_egg_play_random();
                spike.egg_playing = 1;
            } else {
                spike_egg_next();
            }
        }
        break;
    }

    case STATE_DETONATED: {
        uint8_t k0 = key0_released();
        if (!spike_audio_is_busy() && !spike.main_audio_done) {
            spike.main_audio_done = 1;
        }
        if (k0 && spike.main_audio_done) {
            if (!spike.egg_playing) {
                spike_egg_play_random();
                spike.egg_playing = 1;
            } else {
                spike_egg_next();
            }
        }
        break;
    }
    }

    spike_lcd_update();
}

/*──────────────────────────────────────────────────────────────
 * KEY_UP hold time: returns ms held, 0 if not held.
 * Uses raw GPIO read (no debounce) with simple state tracking.
 *──────────────────────────────────────────────────────────────*/
static uint32_t key_up_hold_ms(void)
{
    static uint32_t press_start = 0;
    static uint8_t  held = 0;
    uint32_t now = HAL_GetTick();

    if (WK_UP == 1) {
        if (!held) {
            press_start = now;
            held = 1;
        }
        return now - press_start;
    } else {
        held = 0;
        press_start = 0;
        return 0;
    }
}

/* KEY1 (PE3) hold time — active low */
static uint32_t key1_hold_ms(void)
{
    static uint32_t press_start = 0;
    static uint8_t  held = 0;
    uint32_t now = HAL_GetTick();

    if (KEY1 == 0) {  /* active low */
        if (!held) {
            press_start = now;
            held = 1;
        }
        return now - press_start;
    } else {
        held = 0;
        press_start = 0;
        return 0;
    }
}

/* KEY0 release detection: returns 1 on rising edge (release) */
static uint8_t key0_released(void)
{
    static uint8_t was_pressed = 0;
    uint8_t pressed = (KEY0 == 0);  /* active low: 0=pressed, 1=released */
    uint8_t result = 0;

    if (!pressed && was_pressed) result = 1;
    was_pressed = pressed;
    return result;
}

/*──────────────────────────────────────────────────────────────
 * State transition handler
 *──────────────────────────────────────────────────────────────*/
static void spike_enter_state(spike_state_t new_state)
{
    spike.state = new_state;

    switch (new_state) {
    case STATE_UNDEPLOYED:
        LED0 = 1; LED1 = 1;
        pcm_stop();
        LCD_Clear(COLOR_BG);
        Show_Str(BAR_X, 100, 400, 24, (uint8_t *)"\xB0\xB4KEY_UP\xB2\xBF\xCA\xF0\xB1\xAC\xC4\xDC\xC6\xF7", 24, 0);
        Show_Str(BAR_X, 130, 400, 24, (uint8_t *)"PRESS KEY_UP TO PLANT", 24, 0);
        spike.countdown_start_ms = 0;
        spike.defuse_press_ms = 0;
        spike.defuse_progress = 0.0f;
        spike.defuse_saved = 0.0f;
        spike.defuse_half_done = 0;
        spike.main_audio_done = 0;
        spike.egg_playing = 0;
        spike.egg_text_switched = 0;
        spike.was_defusing = 0;
        break;

    case STATE_DEPLOYING:
        spike.defuse_progress = 0.0f;
        spike.defuse_saved = 0.0f;
        spike.defuse_half_done = 0;
        spike_audio_play_start("0:/SOUNDS/planting.mp3");
        LCD_Clear(COLOR_BG);
        spike_draw_progress_bar(0.0f, 0);
        Show_Str(BAR_X, 120, 400, 24, (uint8_t *)"\xD5\xFD\xD4\xDA\xB2\xBF\xCA\xF0", 24, 0);
        Show_Str(BAR_X, 150, 400, 24, (uint8_t *)"SPIKE PLANTING", 24, 0);
        break;

    case STATE_DEPLOYED:
        /* Only reset countdown on fresh deploy, not when returning from defuse */
        if (spike.countdown_start_ms == 0) {
            spike.countdown_start_ms = HAL_GetTick();
            spike.countdown_elapsed = 0;
            spike.led_phase_idx = 0;
            spike.led_phase_elapsed = 0;
            spike_audio_play_start("0:/SOUNDS/planted.mp3");
        }
        LED0 = 1; LED1 = 1;
        LCD_Clear(COLOR_BG);
        spike_draw_progress_bar(spike.defuse_progress, 1);
        Show_Str(BAR_X, 120, 400, 24, (uint8_t *)"\xB1\xAC\xC4\xDC\xC6\xF7\xD2\xD1\xB2\xBF\xCA\xF0", 24, 0);
        Show_Str(BAR_X, 150, 400, 24, (uint8_t *)"SPIKE PLANTED", 24, 0);
        Show_Str(BAR_X, 180, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x31\xB2\xF0\xB3\xFD", 24, 0);
        Show_Str(BAR_X, 210, 400, 24, (uint8_t *)"PRESS KEY1 TO DEFUSE", 24, 0);
        break;

    case STATE_DEFUSING:
        spike.defuse_press_ms = HAL_GetTick();
        LED1 = 0;
        if (spike.defuse_half_done)
            spike_audio_pause_and_play("0:/SOUNDS/defuse_start_2.mp3");
        else
            spike_audio_pause_and_play("0:/SOUNDS/defuse_start_1.mp3");
        LCD_Clear(COLOR_BG);
        spike_draw_progress_bar(spike.defuse_saved, 1);
        Show_Str(BAR_X, 120, 400, 24, (uint8_t *)"\xD5\xFD\xD4\xDA\xB2\xF0\xB3\xFD", 24, 0);
        Show_Str(BAR_X, 150, 400, 24, (uint8_t *)"DEFUSING", 24, 0);
        Show_Str(BAR_X, 180, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x31\xB2\xF0\xB3\xFD", 24, 0);
        Show_Str(BAR_X, 210, 400, 24, (uint8_t *)"PRESS KEY1 TO DEFUSE", 24, 0);
        break;

    case STATE_DEFUSED:
        LED0 = 1; LED1 = 0;
        pcm_stop();
        /* Reload defused eggs (may have been overwritten by detonated dir) */
        spike_egg_load_dir("0:/SOUNDS/Easter_eggs/defused");
        spike.display_time = 45.0f - (float)(HAL_GetTick() - spike.countdown_start_ms) / 1000.0f;
        if (spike.display_time < 0.0f) spike.display_time = 0.0f;
        spike.main_audio_done = 0;
        spike.egg_playing = 0;
        spike.egg_text_switched = 0;
        spike_audio_play_start("0:/SOUNDS/defused.mp3");
        LCD_Clear(COLOR_BG);
        spike_update_display_time("+");
        Show_Str(BAR_X, 150, 400, 24, (uint8_t *)"\xB1\xAC\xC4\xDC\xC6\xF7\xD2\xD1\xB2\xF0\xB3\xFD", 24, 0);
        Show_Str(BAR_X, 180, 400, 24, (uint8_t *)"SPIKE DEFUSED", 24, 0);
        Show_Str(BAR_X, 210, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x30\xB2\xA5\xB7\xC5\xB2\xCA\xB5\xB0", 24, 0);
        break;

    case STATE_DETONATED:
        LED0 = 0; LED1 = 1;
        pcm_stop();
        spike.main_audio_done = 0;
        spike.egg_playing = 0;
        spike.egg_text_switched = 0;
        spike_audio_play_start("0:/SOUNDS/boom.mp3");
        spike_egg_load_dir("0:/SOUNDS/Easter_eggs/detonated");
        LCD_Clear(COLOR_BG);
        if (spike.was_defusing) spike_update_display_time("-");
        Show_Str(BAR_X, 130, 400, 24, (uint8_t *)"\xB1\xAC\xC4\xDC\xC6\xF7\xD2\xD1\xC6\xF4\xB6\xAF", 24, 0);
        Show_Str(BAR_X, 160, 400, 24, (uint8_t *)"SPIKE DETONATED", 24, 0);
        Show_Str(BAR_X, 190, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x30\xB2\xA5\xB7\xC5\xB2\xCA\xB5\xB0", 24, 0);
        break;
    }
}

/*──────────────────────────────────────────────────────────────
 * LED blinking update
 *──────────────────────────────────────────────────────────────*/
void spike_led_update(void)
{
    const led_phase_t *ph;
    uint32_t phase_elapsed, cycle, in_cycle;

    if (spike.state == STATE_DEPLOYED || spike.state == STATE_DEFUSING) {
        uint32_t elapsed = spike.countdown_elapsed;
        uint32_t phase_start = 0;
        uint8_t i;

        for (i = 0; i < 6; i++) {
            if (elapsed < phase_start + led_phases[i].duration_ms) {
                spike.led_phase_idx = i;
                spike.led_phase_elapsed = elapsed - phase_start;
                break;
            }
            phase_start += led_phases[i].duration_ms;
        }
        if (i >= 6) {
            spike.led_phase_idx = 5;
            spike.led_phase_elapsed = elapsed - 44500;
        }

        ph = &led_phases[spike.led_phase_idx];
        phase_elapsed = spike.led_phase_elapsed;
        cycle = ph->on_ms + ph->off_ms;
        in_cycle = phase_elapsed % cycle;

        LED0 = (in_cycle < ph->on_ms) ? 0 : 1;
    } else if (spike.state == STATE_DETONATED) {
        LED0 = 0;
    } else if (spike.state != STATE_DEFUSED) {
        LED0 = 1;
    }
}

/*──────────────────────────────────────────────────────────────
 * LCD update
 *──────────────────────────────────────────────────────────────*/
void spike_lcd_update(void)
{
    switch (spike.state) {
    case STATE_DEPLOYING: {
        uint32_t h = key_up_hold_ms();
        float p = (float)h / 4000.0f;
        if (p > 1.0f) p = 1.0f;
        spike_draw_progress_bar(p, 0);
        break;
    }
    case STATE_DEPLOYED:
        spike_draw_progress_bar(spike.defuse_progress, 1);
        break;
    case STATE_DEFUSING:
        spike_draw_progress_bar(spike.defuse_progress, 1);
        break;
    case STATE_DEFUSED:
        spike_update_display_time("+");
        if (spike.egg_playing && !spike.egg_text_switched) {
            LCD_Fill(BAR_X, 210, BAR_X + 400, 238, COLOR_BG);
            Show_Str(BAR_X, 210, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x30\xC7\xD0\xBB\xBB\xB2\xCA\xB5\xB0", 24, 0);
            spike.egg_text_switched = 1;
        }
        break;
    case STATE_DETONATED:
        if (spike.was_defusing) spike_update_display_time("-");
        if (spike.egg_playing && !spike.egg_text_switched) {
            LCD_Fill(BAR_X, 190, BAR_X + 400, 218, COLOR_BG);
            Show_Str(BAR_X, 190, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x30\xC7\xD0\xBB\xBB\xB2\xCA\xB5\xB0", 24, 0);
            spike.egg_text_switched = 1;
        }
        break;
    default:
        break;
    }
}

static void spike_draw_progress_bar(float progress, uint8_t show_midline)
{
    uint16_t fill_w, old_color;
    old_color = POINT_COLOR;
    POINT_COLOR = BLACK;
    LCD_DrawRectangle(BAR_X - 1, BAR_Y - 1, BAR_X + BAR_W + 1, BAR_Y + BAR_H + 1);
    LCD_Fill(BAR_X, BAR_Y, BAR_X + BAR_W, BAR_Y + BAR_H, COLOR_BG);
    fill_w = (uint16_t)(progress * (float)BAR_W);
    if (fill_w > 0)
        LCD_Fill(BAR_X, BAR_Y + 2, BAR_X + fill_w, BAR_Y + BAR_H - 2, COLOR_BAR_FILL);
    if (show_midline)
        LCD_DrawLine(BAR_MID, BAR_Y + 2, BAR_MID, BAR_Y + BAR_H - 2);
    POINT_COLOR = old_color;
}

static void spike_lcd_clear_area(uint16_t y, uint16_t h)
{
    LCD_Fill(0, y, 480, y + h, COLOR_BG);
}

static void spike_update_display_time(const char *prefix)
{
    char buf[32];
    sprintf(buf, "%s %05.2f s", prefix, spike.display_time);
    spike_lcd_clear_area(BAR_X, 40);
    Show_Str(BAR_X, 100, 460, 24, (uint8_t *)buf, 24, 0);
}

/*──────────────────────────────────────────────────────────────
 * Easter egg management
 *──────────────────────────────────────────────────────────────*/
void spike_egg_load_dir(const char *dir)
{
    DIR mp3dir;
    FILINFO fno;
    FRESULT res;
    uint8_t i = 0;
    int flen;

    strcpy(spike.egg_dir, dir);
    spike.egg_count = 0;
    spike.egg_index = 0;

    res = f_opendir(&mp3dir, (const TCHAR *)dir);
    if (res != FR_OK) return;

    while (i < 20) {
        res = f_readdir(&mp3dir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        flen = strlen(fno.fname);
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

void spike_egg_play_random(void)
{
    if (spike.egg_count == 0) return;
    spike.egg_index = ((uint32_t)HAL_GetTick() / 100) % spike.egg_count;
    sprintf(spike.egg_current, "%s/%s", spike.egg_dir, spike.egg_files[spike.egg_index]);
    spike_audio_play_start(spike.egg_current);
}

void spike_egg_next(void)
{
    if (spike.egg_count == 0) return;
    spike.egg_index = (spike.egg_index + 1) % spike.egg_count;
    sprintf(spike.egg_current, "%s/%s", spike.egg_dir, spike.egg_files[spike.egg_index]);
    spike_audio_stop();
    while (spike_audio_is_busy()) spike_audio_feed();
    spike_audio_play_start(spike.egg_current);
}
