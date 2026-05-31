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

FATFS fs[2];

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

    f_mount(&fs[0], "0:", 1);
    f_mount(&fs[1], "1:", 1);

    POINT_COLOR = RED;

    while (font_init())
    {
        LCD_ShowString(30, 50, 200, 16, 16, (uint8_t *)"Font Error!", 16);
        delay_ms(200);
        LCD_Fill(30, 50, 240, 66, WHITE);
    }

    Show_Str(30, 50, 400, 24, (uint8_t *)"Valorant Spike", 24, 0);
    Show_Str(30, 90, 400, 24, (uint8_t *)"按住KEY_UP部署爆能器", 24, 0);
    delay_ms(1500);

    spike_init();

    spike_egg_load_dir("0:/SOUNDS/Easter_eggs/defused");

    LCD_Clear(WHITE);

    while (1)
    {
        spike_loop();
        delay_ms(1);
    }
}
