# Task Plan: Valorant Spike 模拟器

## Goal
基于正点原子战舰V3（STM32F103ZET6），模拟Valorant游戏中"爆能器（Spike）"的部署、拆除、引爆全过程，含音频播放、LED闪烁、LCD显示及彩蛋系统。

## Current Phase
Phase 2

## Phases

### Phase 1: 需求分析与参考学习
- [x] 完整阅读需求文档并充分理解
- [x] 明确引脚分配
- [x] 确定音频方案（PA8 PWM → P11 → 板载扬声器）
- [x] 将SOUNDS和SYSTEM目录复制到SD卡（G盘）
- [x] 获取所有MP3文件时长信息
- [x] 精读音乐播放器参考例程
- [x] 确认项目代码结构和初始化流程
- [x] P11 短接 AIN-PDC（已完成硬件配置）
- **Status:** complete

### Phase 2: 项目工程搭建
- [ ] 2.1 从音乐播放器例程复制完整目录结构
- [ ] 2.2 精简不需要的组件（PICTURE/USMART/FLAC/MALLOC等按需保留）
- [ ] 2.3 在Keil中配置工程（源文件分组、包含路径、ARM Compiler v5）
- [ ] 2.4 修改main.c初始化流程适配本项目
- [ ] 2.5 验证工程可成功编译
- **Status:** in_progress

### Phase 3: 基础驱动适配
- [ ] 3.1 LED驱动（PB5红色, PE5绿色）
- [ ] 3.2 KEY驱动（PA0 KEY_UP, PE4 KEY0，适配长按检测）
- [ ] 3.3 LCD驱动（4.3寸TFTLCD FSMC, GBK24字体）
- [ ] 3.4 VS1053驱动（SPI1, 含LINE_IN监听配置）
- [ ] 3.5 SD卡+FATFS（SDIO方式挂载）
- [ ] 3.6 PWM音频输出（PA8 TIM1_CH1, 含PCM播放ISR）
- **Status:** pending

### Phase 4: 核心应用层实现
- [ ] 4.1 状态机框架（spike.h/c，6状态枚举+转换函数）
- [ ] 4.2 按键处理（长按计时、短按检测、去抖）
- [ ] 4.3 红色LED非阻塞闪烁引擎（6阶段加速规律）
- [ ] 4.4 绿色LED状态控制
- [ ] 4.5 主音频播放（spike_audio.c/h，VS1053播放指定MP3）
- [ ] 4.6 PCM短音效（离线转换defuse_start为PCM数组，TIM2 ISR输出）
- [ ] 4.7 LCD显示引擎（各状态文字+拆除进度条绘制）
- [ ] 4.8 45秒倒计时管理
- [ ] 4.9 拆除进度计算（含50%保留逻辑+剩余时间显示）
- [ ] 4.10 彩蛋系统（随机选曲+KEY0循环切歌+播完不自动切）
- **Status:** pending

### Phase 5: 集成联调
- [ ] 5.1 主循环整合（状态机+按键+LED+音频+LCD）
- [ ] 5.2 完整状态流转测试
- [ ] 5.3 拆除进度逻辑验证（<3.5s/3.5s~7s/>=7s）
- [ ] 5.4 引爆时拆除中的特殊显示验证
- [ ] 5.5 彩蛋切换逻辑验证
- [ ] 5.6 边界条件测试（快速按键、中途复位等）
- **Status:** pending

### Phase 6: 完善与交付
- [ ] 6.1 烧录到开发板实测全部功能
- [ ] 6.2 音频质量和时序验证
- [ ] 6.3 LCD显示效果调优
- [ ] 6.4 全部需求对照逐项验证
- [ ] 6.5 整理最终代码
- **Status:** pending

---

## 详细实施步骤

### Phase 2 详细步骤：工程搭建

#### 2.1 复制目录结构
从 `实验43 音乐播放器实验` 复制以下目录到 `D:\STM32Projects\Valorant_Spike\`：

| 源目录 | 说明 |
|--------|------|
| `CORE/` | CMSIS核心：core_cm3.h, cmsis_armcc.h, startup_stm32f103xe.s 等 |
| `HALLIB/` | STM32 HAL库：全部STM32F1xx_HAL_Driver |
| `SYSTEM/` | delay/, sys/, usart/ |
| `HARDWARE/LED/` | led.c, led.h |
| `HARDWARE/KEY/` | key.c, key.h |
| `HARDWARE/LCD/` | lcd.c, lcd.h, lcd_ex.c 等（FSMC TFTLCD完整驱动） |
| `HARDWARE/VS10XX/` | vs10xx.c, vs10xx.h（VS1053完整驱动） |
| `HARDWARE/SDIO_SDCARD/` | sdio_sdcard.c, sdio_sdcard.h |
| `HARDWARE/SRAM/` | sram.c, sram.h（外部1MB SRAM） |
| `HARDWARE/MALLOC/` | malloc.c, malloc.h（动态内存管理） |
| `HARDWARE/W25QXX/` | w25qxx.c, w25qxx.h（SPI Flash，字库存储） |
| `FATFS/` | ff.c, ff.h, diskio.c, diskio.h, exfuns/ 等 |
| `TEXT/` | text.c, text.h, fontupd.c, fontupd.h（中文显示） |
| `USER/` | main.c, stm32f1xx_it.c, stm32f1xx_hal_conf.h, system_stm32f1xx.c |

#### 2.2 精简不需要的文件
- 删除 `APP/mp3player.c/h`（用自己的spike应用层替代）
- 删除 `PICTURE/` 目录（不需要图片显示）
- 删除 `USMART/` 目录（不需要串口调试组件）
- 删除 `MALLOC/` 如果本项目不动态分配（保留SRAM的malloc以备不时之需）
- 保留 `FATFS/exfuns/` 中的 exfuns.c/h（FATFS辅助函数）

#### 2.3 创建新文件
- `APP/spike.h` — 状态机枚举、全局状态结构体
- `APP/spike.c` — 状态机实现
- `APP/spike_audio.h` — 音频播放接口
- `APP/spike_audio.c` — 音频播放（VS1053 + PWM）
- `APP/pcm_data.h` — PCM预转换数据声明
- `APP/pcm_data.c` — defuse_start_1/2 的PCM数据数组
- `HARDWARE/PWMDAC/pwmdac.c` — PA8 PWM音频初始化（从实验21复制）
- `HARDWARE/PWMDAC/pwmdac.h` — PWM音频接口

#### 2.4 Keil工程配置
- 打开音乐播放器的 `.uvprojx`，另存为新工程
- 源文件分组：CORE / HALLIB / SYSTEM / HARDWARE / FATFS / TEXT / APP / USER
- 包含路径添加所有头文件目录
- 编译器选 ARM Compiler v5（__CC_ARM）
- 定义宏：`STM32F103xE`, `USE_HAL_DRIVER`

#### 2.5 main.c 初始化流程
```c
HAL_Init();
Stm32_Clock_Init(RCC_PLL_MUL9);  // 72MHz
delay_init(72);
uart_init(115200);                // 调试用串口
LED_Init();                       // PB5红, PE5绿
KEY_Init();                       // PA0, PE2~4
LCD_Init();                       // FSMC 4.3寸
SRAM_Init();                      // 外部1MB SRAM
VS_Init();                        // VS1053 SPI1
my_mem_init(SRAMIN);              // 内部内存池
exfuns_init();                    // FATFS缓冲区
f_mount(fs[0], "0:", 1);          // 挂载SD卡
f_mount(fs[1], "1:", 1);          // 挂载SPI Flash
while(font_init());               // 检查/更新字库
TIM1_CH1_PWM_Init(255, 1);       // PA8 PWM, 281.25kHz
spike_init();                     // 初始化状态机
// 进入主循环
while(1) { spike_loop(); }
```

---

### Phase 4 详细步骤：核心应用层

#### 4.1 状态机定义 (spike.h)

```c
typedef enum {
    STATE_UNDEPLOYED,   // 未部署
    STATE_DEPLOYING,    // 正在部署
    STATE_DEPLOYED,     // 已部署
    STATE_DEFUSING,     // 正在拆除
    STATE_DEFUSED,      // 成功拆除
    STATE_DETONATED     // 已引爆
} spike_state_t;

typedef struct {
    spike_state_t state;
    uint32_t deploy_start_ms;     // 部署开始时刻
    uint32_t countdown_start_ms;  // 倒计时开始时刻
    uint32_t defuse_start_ms;     // 拆除开始时刻
    float defuse_progress;        // 拆除进度 0.0~1.0
    float defuse_saved;           // 已保存的拆除进度
    uint8_t defuse_half_saved;    // 是否已保存50%进度
    float time_remaining;         // 剩余时间（秒）
    uint32_t elapsed_ms;          // 已部署状态已过毫秒
    uint8_t led_phase;            // LED当前阶段
    uint32_t led_toggle_ms;       // LED上次翻转时刻
    uint8_t led_on;               // LED当前亮灭
    // 彩蛋
    uint8_t egg_playing;          // 正在播放彩蛋
    uint8_t egg_index;            // 当前彩蛋索引
    char egg_current_file[128];   // 当前彩蛋文件路径
} spike_t;
```

#### 4.2 按键处理（改造KEY_Scan，添加长按检测）

KEY_UP(PA0) 按下时：
- **低电平=按下**（PA0配置为下拉输入，按下接3.3V为高电平）
- 记录按下时刻，每10ms扫描一次
- 短按(<阈值)：通过KEY_Scan的mode 0检测
- 长按：累计press_start以来的时间判断是否达到阈值

需要在key.c中新增：
```c
// 返回KEY_UP的持续按下时间(ms)，未按下返回0
uint32_t KEY_Up_PressTime(void);
```

#### 4.3 红色LED非阻塞闪烁引擎

使用1ms定时器中断（TIM3），维护全局tick。在spike_loop()中：
```c
// LED闪烁用状态机而非延时
if (state == STATE_DEPLOYED || state == STATE_DEFUSING) {
    uint32_t elapsed = tick - countdown_start_ms;
    // 根据elapsed确定当前阶段和亮/灭时长
    // 在led_toggle_ms到期时翻转LED
}
```

6阶段参数表（用结构体数组存储）：
```c
typedef struct {
    uint32_t duration_ms;  // 本阶段持续时长
    uint16_t on_ms;        // 亮时长
    uint16_t off_ms;       // 灭时长
} led_phase_t;

const led_phase_t led_phases[6] = {
    {25000, 50, 950},   // 0-25s
    {10000, 50, 450},   // 25-35s
    {5000,  50, 200},   // 35-40s
    {2500,  50, 75},    // 40-42.5s
    {1500,  25, 25},    // 42.5-44s
    {1000,  12, 13},    // 44-45s
};
```

每个阶段内：(on_ms + off_ms) 为一个周期，阶段内重复。`elapsed_ms`用于定位当前处于哪个阶段。

#### 4.4 PWM短音效播放

##### PCM数据准备
使用Python离线转换defuse_start_1.mp3和defuse_start_2.mp3：
- 8-bit unsigned, 22050Hz mono
- 用pydub库：`AudioSegment.from_mp3()` → resample → export raw
- 将raw数据转换为C数组写入 `pcm_data.c`

##### 播放机制
```c
// 全局PCM播放状态
typedef struct {
    const uint8_t *data;    // PCM数据指针
    uint32_t length;        // 数据长度
    uint32_t position;      // 当前播放位置
    uint8_t playing;        // 正在播放标志
} pcm_player_t;

// TIM2中断服务程序(22050Hz)
void TIM2_IRQHandler(void) {
    if (pcm_player.playing) {
        TIM1->CCR1 = pcm_player.data[pcm_player.position++];
        if (pcm_player.position >= pcm_player.length) {
            pcm_player.playing = 0;
            TIM1->CCR1 = 0;  // 静音
        }
    }
    HAL_TIM_IRQHandler(&htim2);
}

void pcm_play_start(const uint8_t *data, uint32_t len) {
    pcm_player.data = data;
    pcm_player.length = len;
    pcm_player.position = 0;
    pcm_player.playing = 1;
}
```

#### 4.5 主音频播放 (spike_audio.c)

```c
// 播放SD卡上的MP3文件（非阻塞）
// 返回0=播放中, 1=播放完毕, 0xFF=错误
uint8_t spike_audio_play(const char *path);

// 停止当前播放
void spike_audio_stop(void);

// 检查播放状态
uint8_t spike_audio_is_playing(void);
```

参考mp3_play_song()实现，但改为：
- 在main循环中分时调用（每次喂32字节）
- 不阻塞（用状态标志位）
- 支持中途停止（设置stop_flag）

VS1053 LINE_IN监听：在VS_Init()后配置SCI_MODE寄存器的SM_LINE1位：
```c
uint16_t mode = VS_RD_Reg(SPI_MODE);
mode |= 1 << 14;  // SM_LINE1
VS_WR_Cmd(SPI_MODE, mode);
```

#### 4.6 LCD显示引擎

##### 进度条绘制
```
矩形框: (x0, y0) → (x1, y1), 长远大于宽
中线: 竖线在 x_mid = x0 + (x1-x0)/2
填充: 从x0到 x0 + progress*(x1-x0)
```
- 画矩形框（空心）：`LCD_DrawRectangle(x0, y0, x1, y1)`
- 画中线：`LCD_DrawLine(x_mid, y0, x_mid, y1)`，颜色加深以示醒目
- 画填充：`LCD_Fill(x0+1, y0+1, x0+progress*w, y1-1, color)`
- 回退动画：progress变小时先清除再重画

##### 各状态LCD显示内容

| 状态 | 第1行 | 第2行 | 第3行 | 第4行 |
|------|-------|-------|-------|-------|
| UNDEPLOYED | (清屏) | | | |
| DEPLOYING | 正在部署 | SPIKE PLANTING | | |
| DEPLOYED | [进度条0%] | 爆能器已部署 | SPIKE PLANTED | |
| DEFUSING | [进度条填充中] | 正在拆除 | DEFUSING | |
| DEFUSED | xx.xx | 爆能器已拆除 | SPIKE DEFUSED | 按KEY0切换彩蛋 |
| DETONATED(拆除中) | xx.xx | 爆能器已启动 | SPIKE DETONATED | 按KEY0切换彩蛋 |
| DETONATED(非拆除) | 爆能器已启动 | SPIKE DETONATED | 按KEY0切换彩蛋 | |

##### 布局参数（480×272屏，GBK24字体=24px高）
```
y_offset = 80;       // 顶部留白
progress_y = 80;     // 进度条Y坐标
progress_h = 30;     // 进度条高度
progress_w = 400;    // 进度条宽度
progress_x = 40;     // 进度条X起点
line_spacing = 30;   // 行间距
```

#### 4.7 倒计时与拆除时间计算

- countdown_start_ms: 进入DEPLOYED状态时的tick
- 任意时刻：`elapsed = tick - countdown_start_ms`
- 剩余时间：`45.0f - elapsed / 1000.0f`
- 拆除中引爆时显示剩余所需时间：`7.0f - defuse_progress * 7.0f`
- 拆除成功时显示：`45.0f - (拆除完成时刻 - countdown_start_ms) / 1000.0f`
- 格式：`Show_Str(x, y, 200, 24, buf, 24, 0)`，用sprintf(buf, "%.2f", time_remaining)

#### 4.8 彩蛋系统

- 进入DEFUSED/DETONATED → 等主音效播完 → 随机选目录中一个mp3 → 播放
- KEY0按下 → 切到目录中下一个mp3 → 播放
- 一个mp3播完不自动切（除非用户按KEY0）
- 目录文件列表在初始化时枚举并存储
- 循环：切到最后一首后再按KEY0回到第一首

#### 4.9 主循环结构 (spike_loop)

```c
void spike_loop(void) {
    // 1. 扫描按键
    uint8_t key = KEY_Scan(0);  // KEY0, KEY_UP等
    uint32_t key_up_time = KEY_Up_PressTime();

    // 2. 更新LED闪烁（基于系统tick）
    spike_led_update();

    // 3. 喂音频数据（VS1053 + PCM）
    spike_audio_feed();

    // 4. 状态机转换处理
    spike_state_machine(key, key_up_time);

    // 5. 更新LCD显示
    spike_lcd_update();

    // 6. 彩蛋切歌检测
    spike_egg_handle(key);
}
```

##### 状态转换逻辑详表

| 当前状态 | 事件 | 动作 | 新状态 |
|----------|------|------|--------|
| UNDEPLOYED | KEY_UP按下 | 播放planting.mp3, 记录press_start | DEPLOYING |
| DEPLOYING | 持续按住>=4s | 停止planting, 播放planted(45s), 记录countdown_start | DEPLOYED |
| DEPLOYING | 松开<4s | 停止planting | UNDEPLOYED |
| DEPLOYED | KEY_UP按下 | 播放defuse_start_1或_2, 绿LED亮, 记录defuse_start | DEFUSING |
| DEPLOYED | 45s到期 | 停止planted, 播放boom, 红LED常亮 | DETONATED |
| DEFUSING | 松开<3.5s | 进度归0, 绿LED灭, 停止defuse_start | DEPLOYED |
| DEFUSING | 松开>=3.5s且<7s | 进度锁50%, 绿LED灭, 停止defuse_start | DEPLOYED |
| DEFUSING | 持续按住>=7s | 停止planted, 播放defused, 红LED灭, 计算剩余时间 | DEFUSED |
| DEFUSING | 45s到期(拆除中引爆) | 停止planted, 播放boom, 绿LED灭, 计算还需时间 | DETONATED |
| DEFUSED | defused.mp3播完 | 随机播彩蛋 | DEFUSED(不变) |
| DEFUSED | KEY0按下 | 切换到下一个彩蛋 | DEFUSED(不变) |
| DETONATED | boom.mp3播完 | 若拆除中引爆且有剩余时间显示, 随机播彩蛋 | DETONATED(不变) |
| DETONATED | KEY0按下 | 切换到下一个彩蛋 | DETONATED(不变) |

---

## Key Questions
1. ✅ VS1053 LINE_IN监听如何配置？（SCI_MODE |= SM_LINE1）
2. ✅ PWM音频路径如何启用？（P11短接AIN-PDC）
3. ✅ 非阻塞式LED闪烁如何实现？（1ms SysTick + 阶段参数表）
4. ✅ 拆除进度条如何精确绘制？（LCD_DrawRectangle + LCD_Fill + LCD_DrawLine）

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| 音频：VS1053主 + PA8 PWM板载 | P11短接AIN-PDC, 零额外硬件；主音频走PHONE外接音箱 |
| PWM音频：TIM1_CH1 PA8 281kHz | 参考PWM DAC实验21, 8位分辨率, 22050Hz采样 |
| PCM数据：8-bit 11025Hz mono | 数据量小(~16KB), 外部SRAM轻松容纳, 音质满足提示音需求 |
| 定时器：SysTick 1ms + TIM2 11025Hz | SysTick驱动LED+倒计时, TIM2驱动PCM音频采样 |
| 非阻塞状态机 | 所有延时基于tick比较, 不用HAL_Delay, 主循环不阻塞 |
| LCD字体：GBK24 (24像素) | 用户指定, 大字体便于阅读 |
| 绿LED：PE5 | IO分配表明确记载 |
| 无软件重置 | 用户确认按复位键即可 |
| 板载扬声器用于短音效 | 方案A, 外接音箱听主音频, 短促提示音从板上发出 |

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
| 无法直接读取.xlsx文件 | 1 | 创建venv安装pandas+openpyxl读取 |
| conda命令不可用 | 1 | 改用Python内置venv |
| P11位置不明确 | 3 | 查V4硬件手册+V3原理图, 确认AIN-PDC短接 |
| P11与P10混淆 | 2 | 确认P10+P11组成6针多功能端口, P11是其中AIN和PWM_AUDIO两针 |

## Notes
- 两个正点原子资料文件夹的内容只能读取和复制, 不能直接修改或移动
- 参考例程：V3 实验43 音乐播放器 / V4 实验42 音乐播放器 / V3 实验21 PWM DAC
- 外部供电：12V 1A
- P11已用跳线帽短接AIN-PDC
- SD卡(G盘)已有SOUNDS和SYSTEM目录
- 更新阶段状态：pending → in_progress → complete
- 决策前重新读取此计划
