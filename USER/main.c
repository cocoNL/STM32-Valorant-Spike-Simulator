#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "key.h"
#include "lcd.h"
#include "sram.h"
#include "malloc.h"
#include "sdio_sdcard.h"
#include "w25qxx.h"
#include "ff.h"
#include "exfuns.h"
#include "text.h"
#include "vs10xx.h"
#include "spike.h"
#include "spike_audio.h"

static uint16_t g_y;

static void init_line(const char *msg)
{
    Show_Str(10, g_y, 460, 24, (uint8_t *)msg, 24, 0);
    g_y += 28;
}

int main(void)
{
    FRESULT sd_res;

    HAL_Init();
    Stm32_Clock_Init(RCC_PLL_MUL9);
    delay_init(72);
    uart_init(115200);
    LED_Init();
    KEY_Init();
    LCD_Init();
    SRAM_Init();
    VS_Init();
    my_mem_init(SRAMIN);
    my_mem_init(SRAMEX);
    exfuns_init();

    f_mount(fs[1], "1:", 1);  /* SPI Flash for font */
    sd_res = f_mount(fs[0], "0:", 1);  /* SD card */

    /* Font init (may update from SD if needed) */
    if (font_init() != 0) {
        POINT_COLOR = RED;
        LCD_Clear(WHITE);
        Show_Str(10, 50, 460, 24, (uint8_t *)"Font: FAIL!", 24, 0);
        while(1);
    }

    /* Draw title */
    POINT_COLOR = BLACK;
    LCD_Clear(WHITE);
    g_y = 84;
    Show_Str(20, g_y, 440, 24, (uint8_t *)"\xCE\xDE\xCE\xB7\xC6\xF5\xD4\xBC\xB1\xAC\xC4\xDC\xC6\xF7\xC4\xA3\xC4\xE2", 24, 0);
    g_y += 32;
    Show_Str(20, g_y, 440, 24, (uint8_t *)"Valorant Spike Simulator", 24, 0);
    g_y += 38;

    spike_startup_gif_open();

    /* Audio init before playback */
    spike_audio_init();
    spike_audio_play_start("0:/SOUNDS/startup/startup.mp3");

    /* Play every 2nd frame interlaced with init + audio feed */
    {
        uint16_t f;
        for (f = 0; f < 10; f += 2) { spike_startup_gif_show(f); spike_audio_feed(); }
    }
    if (sd_res != FR_OK) { init_line("SD: FAIL!"); while(1); }

    { uint16_t f; for (f = 10; f < 20; f += 2) { spike_startup_gif_show(f); spike_audio_feed(); } }

    if (!spike_state_pics_load()) { init_line("Pics: FAIL!"); while(1); }
    { uint16_t f; for (f = 20; f < 30; f += 2) { spike_startup_gif_show(f); spike_audio_feed(); } }

    spike_egg_load_dir("0:/SOUNDS/Easter_eggs/defused");
    if (spike.egg_count == 0) { init_line("Eggs: FAIL!"); while(1); }
    { uint16_t f; for (f = 30; f < 50; f += 2) { spike_startup_gif_show(f); spike_audio_feed(); } }

    { uint16_t f; for (f = 50; f < 80; f += 2) { spike_startup_gif_show(f); spike_audio_feed(); } }

    spike_audio_stop();
    spike_startup_gif_close();

    spike_init();

    while (1)
    {
        spike_loop();
    }
}
