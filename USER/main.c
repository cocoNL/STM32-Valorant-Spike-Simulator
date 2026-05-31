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
#include "pwmdac.h"
#include "pcm_data.h"

static uint16_t g_y;

/* Print a line and move down */
static void init_show(const char *msg)
{
    Show_Str(10, g_y, 460, 24, (uint8_t *)msg, 24, 0);
    g_y += 30;
}

/* Overwrite current line (used to show result after check) */
static void init_result(const char *msg)
{
    g_y -= 30;
    LCD_Fill(10, g_y, 470, g_y + 28, WHITE);
    Show_Str(10, g_y, 460, 24, (uint8_t *)msg, 24, 0);
    g_y += 30;
}

int main(void)
{
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
    exfuns_init();

    POINT_COLOR = BLACK;
    LCD_Clear(WHITE);
    g_y = 10;

    init_show("Sys Init OK");

    /* SD card */
    init_show("SD: checking...");
    if (f_mount(fs[0], "0:", 1) == FR_OK)
        init_result("SD: OK");
    else {
        init_result("SD: FAIL!");
        while(1);
    }

    f_mount(fs[1], "1:", 1);

    /* Font */
    init_show("Font: checking...");
    if (font_init() == 0)
        init_result("Font: OK");
    else {
        init_result("Font: FAIL!");
        while(1);
    }

    /* Audio */
    init_show("Audio: checking...");
    spike_audio_init();
    init_result("Audio: OK");

    /* Onboard speaker */
    init_show("Spk: beep...");
    pcm_play_start(pcm_defuse_start_1, pcm_defuse_start_1_len);
    delay_ms(1000);
    init_result("Spk: OK");

    /* PHONE */
    init_show("Phone: tone...");
    VS_Sine_Test();
    init_result("Phone: OK");

    /* Eggs */
    init_show("Eggs: checking...");
    spike_egg_load_dir("0:/SOUNDS/Easter_eggs/defused");
    init_result("Eggs: OK");

    /* Done */
    init_show("All OK! Starting...");
    delay_ms(2000);
    LCD_Clear(WHITE);

    spike_init();

    while (1)
    {
        spike_loop();
    }
}
