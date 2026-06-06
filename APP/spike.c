#include "spike.h"
#include "spike_audio.h"
#include "pcm_data.h"
#include "piclib.h"
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
static uint8_t pic_key2_released(void);
static void spike_draw_state_pic(void);

void spike_init(void)
{
    memset(&spike, 0, sizeof(spike_t));
    spike.state = STATE_UNDEPLOYED;
    spike.state_pic_idx = -1;
    LED0 = 1; LED1 = 1;
    LCD_Clear(COLOR_BG);
    Show_Str(BAR_X, 100, 400, 24, (uint8_t *)"\xB0\xB4KEY_UP\xB2\xBF\xCA\xF0\xB1\xAC\xC4\xDC\xC6\xF7", 24, 0);
    Show_Str(BAR_X, 130, 400, 24, (uint8_t *)"PRESS KEY_UP TO PLANT", 24, 0);
}

void spike_loop(void)
{
    uint8_t key;
    uint32_t hold;
    uint8_t k2_rel;

    spike_audio_feed();
    spike_audio_resume_if_needed(spike.countdown_elapsed);

    /* Deferred audio stop: play to 1.3s for early deploy release */
    if (spike.deploy_stop_ms > 0 && HAL_GetTick() >= spike.deploy_stop_ms) {
        spike_audio_stop();
        spike.deploy_stop_ms = 0;
    }

    key = KEY_Scan(1);
    hold = key_up_hold_ms();
    {
        uint32_t hold1 = key1_hold_ms();
        if (spike.state == STATE_DEFUSING) hold = hold1;
    }

    k2_rel = pic_key2_released();

    /* Picture display mode: handle all keys, skip normal state machine */
    if (spike.pic_mode) {
        uint8_t k0 = key0_released();
        if (key == WKUP_PRES) spike_pic_prev();
        if (key == KEY1_PRES) spike_pic_next();
        if (k2_rel) { spike_pic_exit(); k2_rel = 0; }
        /* KEY0 egg switching still works in pic mode */
        if (k0 && spike.main_audio_done) {
            if (!spike.egg_playing) {
                spike_egg_play_random();
                spike.egg_playing = 1;
            } else {
                spike_egg_next();
            }
        }
        return;  /* skip normal state machine */
    }

    spike_led_update();

    switch (spike.state) {

    case STATE_UNDEPLOYED:
        if (key == WKUP_PRES) {
            spike_enter_state(STATE_DEPLOYING);
        }
        break;

    case STATE_DEPLOYING: {
        static uint32_t last_hold_val = 0;
        if (hold > 0) last_hold_val = hold;
        if (hold >= 4000) {
            spike_audio_stop();
            spike_enter_state(STATE_DEPLOYED);
            last_hold_val = 0;
        } else if (hold == 0) {
            if (last_hold_val < 1000) {
                /* Held <1s: play to 1.3s then stop */
                spike.deploy_stop_ms = HAL_GetTick() + (1300 - last_hold_val);
                last_hold_val = 0;
                spike_enter_state(STATE_UNDEPLOYED);
            } else {
                /* Held >=1s: stop immediately */
                spike_audio_stop();
                spike_enter_state(STATE_UNDEPLOYED);
                last_hold_val = 0;
            }
        }
        break;
    }

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
        if (!spike_audio_is_busy() && !spike.main_audio_done)
            spike.main_audio_done = 1;
        if (k0 && spike.main_audio_done) {
            if (!spike.egg_playing) {
                spike_egg_play_random();
                spike.egg_playing = 1;
            } else {
                spike_egg_next();
            }
        }
        if (k2_rel) spike_pic_enter();
        break;
    }

    case STATE_DETONATED: {
        uint8_t k0 = key0_released();
        if (!spike_audio_is_busy() && !spike.main_audio_done)
            spike.main_audio_done = 1;
        if (k0 && spike.main_audio_done) {
            if (!spike.egg_playing) {
                spike_egg_play_random();
                spike.egg_playing = 1;
            } else {
                spike_egg_next();
            }
        }
        if (k2_rel) spike_pic_enter();
        break;
    }
    }

    if (!spike.pic_mode)
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
    spike.state_pic_idx = -1;  /* force state pic redraw */

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
        spike.deploy_stop_ms = 0;
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
        Show_Str(BAR_X, 238, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x32\xCF\xD4\xCA\xBE\xCD\xBC\xC6\xAC", 24, 0);
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
        Show_Str(BAR_X, 218, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x32\xCF\xD4\xCA\xBE\xCD\xBC\xC6\xAC", 24, 0);
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

    spike_draw_state_pic();
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

/*──────────────────────────────────────────────────────────────
 * State picture display
 *──────────────────────────────────────────────────────────────*/
/* Pre-loaded state picture data from SD card (stored in external SRAM) */
static u16 *sram_pics[8];  /* pointers to pixel data in SRAM */
static u16 sram_pic_w[8], sram_pic_h[8];  /* dimensions */

/* Load all state pics from SD into external SRAM at init */
uint8_t spike_state_pics_load(void)
{
    static const char *names[] = {
        "not_planted","planting","planted","half_defused",
        "defuse_1","defuse_2","defused","detonated"
    };
    char path[64];
    FIL f;
    UINT br;
    u16 wh[2];
    int i;

    for (i = 0; i < 8; i++) {
        sprintf(path, "0:/PICS/spike_pics/%s.bin", names[i]);
        if (f_open(&f, path, FA_READ) != FR_OK) continue;

        /* Read width and height */
        f_read(&f, wh, 4, &br);
        sram_pic_w[i] = wh[0];
        sram_pic_h[i] = wh[1];

        /* Allocate SRAM and read pixel data */
        sram_pics[i] = (u16 *)mymalloc(SRAMEX, wh[0] * wh[1] * 2);
        if (sram_pics[i])
            f_read(&f, sram_pics[i], wh[0] * wh[1] * 2, &br);
        f_close(&f);
    }
    return (sram_pics[0] != NULL) ? 1 : 0;
}

static void spike_draw_state_pic(void)
{
    const u16 *pic = NULL;
    uint16_t pic_w = 0, pic_h = 0;
    uint16_t x, y;
    int idx = -1, i;

    if (spike.pic_mode) return;

    switch (spike.state) {
    case STATE_UNDEPLOYED: idx = 0; break;
    case STATE_DEPLOYING:  idx = 1; break;
    case STATE_DEPLOYED:   idx = spike.defuse_half_done ? 3 : 2; break;
    case STATE_DEFUSING:   idx = (spike.defuse_progress >= 0.5f) ? 5 : 4; break;
    case STATE_DEFUSED:    idx = 6; break;
    case STATE_DETONATED:  idx = 7; break;
    default: return;
    }

    if (idx == spike.state_pic_idx) return;
    spike.state_pic_idx = idx;

    pic = sram_pics[idx];
    if (!pic) return;
    pic_w = sram_pic_w[idx];
    pic_h = sram_pic_h[idx];

    i = 0;
    for (y = 272 - pic_h + 450; y < 272 + 450; y++) {
        for (x = (480 - pic_w) / 2; x < (480 + pic_w) / 2; x++) {
            LCD_Fast_DrawPoint(x, y, pic[i++]);
        }
    }
}

/*──────────────────────────────────────────────────────────────
 * Picture display
 *──────────────────────────────────────────────────────────────*/
static uint8_t pic_key2_released(void)
{
    static uint8_t was = 0;
    uint8_t pressed = (KEY2 == 0);
    uint8_t result = 0;
    if (!pressed && was) result = 1;
    was = pressed;
    return result;
}

void spike_pic_load_dir(void)
{
    DIR picdir;
    FILINFO fno;
    FRESULT res;
    uint16_t i = 0;
    int flen;

    spike.pic_count = 0;
    spike.pic_index = 0;

    res = f_opendir(&picdir, "0:/PICS");
    if (res != FR_OK) return;

    while (i < 50) {
        res = f_readdir(&picdir, &fno);
        if (res != FR_OK || fno.fname[0] == 0) break;
        flen = strlen(fno.fname);
        if (flen > 4 && fno.fname[flen-4] == '.'
            && (fno.fname[flen-3] == 'b' || fno.fname[flen-3] == 'B')
            && (fno.fname[flen-2] == 'm' || fno.fname[flen-2] == 'M')
            && (fno.fname[flen-1] == 'p' || fno.fname[flen-1] == 'P')) {
            strcpy(spike.pic_files[i], fno.fname);
            i++;
        }
    }
    spike.pic_count = i;
    f_closedir(&picdir);

    /* Sort alphabetically */
    {
        uint16_t a, b;
        char tmp[64];
        for (a = 0; a < spike.pic_count; a++) {
            for (b = a + 1; b < spike.pic_count; b++) {
                if (strcmp(spike.pic_files[a], spike.pic_files[b]) > 0) {
                    strcpy(tmp, spike.pic_files[a]);
                    strcpy(spike.pic_files[a], spike.pic_files[b]);
                    strcpy(spike.pic_files[b], tmp);
                }
            }
        }
    }
}

void spike_pic_show(uint16_t index)
{
    char path[128];
    char name[64];
    char *dot;
    if (spike.pic_count == 0) return;
    sprintf(path, "0:/PICS/%s", spike.pic_files[index]);

    LCD_Clear(BLACK);
    /* Draw text first, then load image */

    /* Extract filename without extension */
    strcpy(name, spike.pic_files[index]);
    dot = strrchr(name, '.');
    if (dot) *dot = 0;    {
        char cnt[16];
        sprintf(cnt, "%d / %d", index + 1, spike.pic_count);
        LCD_Fill(0, 674, 480, 798, BLACK);
        Show_Str(0, 676, 480, 24, (uint8_t *)cnt, 24, 0);
        Show_Str(0, 700, 480, 24, (uint8_t *)name, 24, 0);
    }
    Show_Str(0, 724, 480, 24, (uint8_t *)"KEY_UP����һ��", 24, 0);
    Show_Str(0, 748, 480, 24, (uint8_t *)"KEY1����һ��", 24, 0);
    Show_Str(0, 772, 480, 24, (uint8_t *)"KEY2���ر�ͼƬ", 24, 0);

    /* Load image (fills area, text stays on top) */
    ai_load_picfile((const u8 *)path, 0, 0, 480, 272, 1);
}

void spike_pic_show_random(void)
{
    if (spike.pic_count == 0) return;
    spike.pic_index = ((uint32_t)HAL_GetTick() / 100) % spike.pic_count;
    spike_pic_show(spike.pic_index);
}

void spike_pic_enter(void)
{
    spike.pic_mode = 1;
    piclib_init();
    pic_phy.yield = spike_audio_feed;  /* feed audio during image decode */
    spike_pic_load_dir();
    spike_pic_show_random();
}

void spike_pic_exit(void)
{
    spike.pic_mode = 0;
    spike.state_pic_idx = -1;  /* force state pic redraw */
    /* Redraw state display without re-triggering audio/time reset */
    LCD_Clear(COLOR_BG);
    switch (spike.state) {
    case STATE_DEFUSED:
        spike_update_display_time("+");
        Show_Str(BAR_X, 150, 400, 24, (uint8_t *)"\xB1\xAC\xC4\xDC\xC6\xF7\xD2\xD1\xB2\xF0\xB3\xFD", 24, 0);
        Show_Str(BAR_X, 180, 400, 24, (uint8_t *)"SPIKE DEFUSED", 24, 0);
        if (spike.egg_playing)
            Show_Str(BAR_X, 210, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x30\xC7\xD0\xBB\xBB\xB2\xCA\xB5\xB0", 24, 0);
        else
            Show_Str(BAR_X, 210, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x30\xB2\xA5\xB7\xC5\xB2\xCA\xB5\xB0", 24, 0);
        Show_Str(BAR_X, 238, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x32\xCF\xD4\xCA\xBE\xCD\xBC\xC6\xAC", 24, 0);
        break;
    case STATE_DETONATED:
        if (spike.was_defusing) spike_update_display_time("-");
        Show_Str(BAR_X, 130, 400, 24, (uint8_t *)"\xB1\xAC\xC4\xDC\xC6\xF7\xD2\xD1\xC6\xF4\xB6\xAF", 24, 0);
        Show_Str(BAR_X, 160, 400, 24, (uint8_t *)"SPIKE DETONATED", 24, 0);
        if (spike.egg_playing)
            Show_Str(BAR_X, 190, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x30\xC7\xD0\xBB\xBB\xB2\xCA\xB5\xB0", 24, 0);
        else
            Show_Str(BAR_X, 190, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x30\xB2\xA5\xB7\xC5\xB2\xCA\xB5\xB0", 24, 0);
        Show_Str(BAR_X, 218, 400, 24, (uint8_t *)"\xB0\xB4\x4B\x45\x59\x32\xCF\xD4\xCA\xBE\xCD\xBC\xC6\xAC", 24, 0);
        break;
    default: break;
    }
}

void spike_pic_next(void)
{
    if (spike.pic_count == 0) return;
    spike.pic_index = (spike.pic_index + 1) % spike.pic_count;
    spike_pic_show(spike.pic_index);
}

#define STARTUP_FRAME_W  356
#define STARTUP_FRAME_H  200
#define STARTUP_FRAME_SZ (STARTUP_FRAME_W * STARTUP_FRAME_H * 2)

static uint8_t *gif_buf;
static FIL    gif_file;
static uint8_t gif_file_open;

void spike_startup_gif_open(void)
{
    gif_buf = (uint8_t *)mymalloc(SRAMEX, STARTUP_FRAME_SZ);
    gif_file_open = 0;
}

void spike_startup_gif_close(void)
{
    if (gif_file_open) f_close(&gif_file);
    if (gif_buf) { myfree(SRAMEX, gif_buf); gif_buf = NULL; }
}

void spike_startup_gif_show(uint16_t frame)
{
    UINT br;
    uint16_t x, y, i;
    uint16_t *px;

    if (!gif_buf) return;

    if (!gif_file_open) {
        if (f_open(&gif_file, "0:/PICS/startup/all.bin", FA_READ) != FR_OK) return;
        gif_file_open = 1;
    }

    /* Seek to frame: 4-byte header + frame * frame_size */
    f_lseek(&gif_file, 4 + (DWORD)STARTUP_FRAME_SZ * frame);
    f_read(&gif_file, gif_buf, STARTUP_FRAME_SZ, &br);
    px = (uint16_t *)gif_buf;

    i = 0;
    for (y = 272 - STARTUP_FRAME_H + 450; y < 272 + 450; y++) {
        LCD_SetCursor((480 - STARTUP_FRAME_W) / 2, y);
        LCD_WriteRAM_Prepare();
        for (x = 0; x < STARTUP_FRAME_W; x++) {
            LCD_WriteRAM(px[i++]);
        }
    }
}

void spike_pic_prev(void)
{
    if (spike.pic_count == 0) return;
    if (spike.pic_index == 0)
        spike.pic_index = spike.pic_count - 1;
    else
        spike.pic_index--;
    spike_pic_show(spike.pic_index);
}
