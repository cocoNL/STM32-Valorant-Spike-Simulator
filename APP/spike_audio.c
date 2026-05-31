#include "spike_audio.h"
#include "vs10xx.h"
#include "sdio_sdcard.h"
#include "malloc.h"
#include "pcm_data.h"
#include "pwmdac.h"
#include "delay.h"

spike_audio_t audio_player;
pcm_player_t   pcm_player;

static TIM_HandleTypeDef TIM2_Handler;

/* Allocate audio buffer from SRAM */
#define AUDIO_BUF_SIZE  4096

void spike_audio_init(void)
{
    audio_player.playing = 0;
    audio_player.done = 0;
    audio_player.stop_req = 0;
    audio_player.buf = (uint8_t *)mymalloc(SRAMIN, AUDIO_BUF_SIZE);
    audio_player.buf_pos = 0;
    audio_player.buf_len = 0;

    pcm_player.playing = 0;
    pcm_player.data = NULL;
    pcm_player.len = 0;
    pcm_player.pos = 0;

    TIM1_CH1_PWM_Init(255, 1);  /* 281.25kHz PWM on PA8 */
    TIM_SetTIM1Compare1(0);      /* silence initially */
    pcm_timer_init();
}

/* Enable VS1053 LINE_IN monitoring (SM_LINE1 = bit 14) */
void vs_linein_monitor_enable(void)
{
    uint16_t mode;
    mode = VS_RD_Reg(SPI_MODE);
    mode |= (1 << 14);  /* SM_LINE1 */
    VS_WR_Cmd(SPI_MODE, mode);
}

/* Start playing an MP3 file via VS1053 */
void spike_audio_play_start(const char *path)
{
    if (!audio_player.buf) return;

    /* Stop any current playback */
    if (audio_player.playing) {
        spike_audio_stop();
        while (audio_player.playing) {
            spike_audio_feed();
        }
    }

    if (f_open(&audio_player.file, (const TCHAR *)path, FA_READ) != FR_OK) {
        audio_player.playing = 0;
        return;
    }

    VS_Restart_Play();
    VS_Set_All();
    VS_Reset_DecodeTime();
    VS_SPI_SpeedHigh();

    audio_player.playing = 1;
    audio_player.done = 0;
    audio_player.stop_req = 0;
    audio_player.buf_pos = 0;
    audio_player.buf_len = 0;
}

/* Stop VS1053 playback */
void spike_audio_stop(void)
{
    if (audio_player.playing) {
        audio_player.stop_req = 1;
    }
}

/* Non-blocking audio feed - call from main loop */
void spike_audio_feed(void)
{
    UINT br;
    uint16_t i;

    if (!audio_player.playing) return;

    /* Handle stop request */
    if (audio_player.stop_req) {
        f_close(&audio_player.file);
        audio_player.playing = 0;
        audio_player.stop_req = 0;
        audio_player.done = 0;
        VS_Soft_Reset();
        return;
    }

    /* Check if VS1053 is ready for data */
    if (VS_DQ == 0) return;

    /* Refill buffer if needed */
    if (audio_player.buf_pos >= audio_player.buf_len) {
        f_read(&audio_player.file, audio_player.buf, AUDIO_BUF_SIZE, &br);
        audio_player.buf_pos = 0;
        audio_player.buf_len = br;

        if (br == 0) {
            /* File finished */
            f_close(&audio_player.file);
            audio_player.playing = 0;
            audio_player.done = 1;
            return;
        }
    }

    /* Send up to 32 bytes */
    i = 0;
    while (i < 32 && audio_player.buf_pos < audio_player.buf_len) {
        if (VS_Send_MusicData(audio_player.buf + audio_player.buf_pos) == 0) {
            audio_player.buf_pos += 32;
            i += 32;
        }
    }
}

uint8_t spike_audio_is_busy(void)
{
    return audio_player.playing;
}

/* PCM player via PWM (TIM2 ISR) */
/* TIM2 interrupt - update PWM duty with next PCM sample */
void TIM2_IRQHandler(void)
{
    if (pcm_player.playing && pcm_player.data) {
        if (pcm_player.pos < pcm_player.len) {
            TIM_SetTIM1Compare1(pcm_player.data[pcm_player.pos]);
            pcm_player.pos++;
        } else {
            TIM_SetTIM1Compare1(0);
            pcm_player.playing = 0;
        }
    }
    __HAL_TIM_CLEAR_IT(&TIM2_Handler, TIM_IT_UPDATE);
}

/* Start PCM playback */
void pcm_play_start(const uint8_t *data, uint32_t len)
{
    pcm_player.data = data;
    pcm_player.len = len;
    pcm_player.pos = 0;
    pcm_player.playing = 1;
}

/* Stop PCM playback */
void pcm_stop(void)
{
    pcm_player.playing = 0;
    pcm_player.pos = 0;
    TIM_SetTIM1Compare1(0);
}

/* Initialize TIM2 for PCM sample rate timing */
void pcm_timer_init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    TIM2_Handler.Instance = TIM2;
    TIM2_Handler.Init.Prescaler = 8;        /* 72MHz / 9 = 8MHz */
    TIM2_Handler.Init.CounterMode = TIM_COUNTERMODE_UP;
    TIM2_Handler.Init.Period = 725;         /* 8MHz / 726 ≈ 11019Hz */
    TIM2_Handler.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    TIM2_Handler.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&TIM2_Handler);

    __HAL_TIM_CLEAR_IT(&TIM2_Handler, TIM_IT_UPDATE);
    HAL_TIM_Base_Start_IT(&TIM2_Handler);

    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}
