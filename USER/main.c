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
    g_y = 24;
    Show_Str(20, g_y, 440, 24, (uint8_t *)"\xCE\xDE\xCE\xB7\xC6\xF5\xD4\xBC\xB1\xAC\xC4\xDC\xC6\xF7\xC4\xA3\xC4\xE2", 24, 0);
    g_y += 32;
    Show_Str(20, g_y, 440, 24, (uint8_t *)"Valorant Spike Simulator", 24, 0);
    g_y += 38;

    /* SD card */
    if (sd_res != FR_OK) { init_line("SD: FAIL!"); while(1); }

    /* Font already checked above */

    /* Audio */
    spike_audio_init();

    /* State pics */
    if (!spike_state_pics_load()) { init_line("Pics: FAIL!"); while(1); }

    /* Eggs */
    spike_egg_load_dir("0:/SOUNDS/Easter_eggs/defused");
    if (spike.egg_count == 0) { init_line("Eggs: FAIL!"); while(1); }

    delay_ms(2000);
    spike_init();

    while (1)
    {
        spike_loop();
    }
}
