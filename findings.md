# Findings & Decisions

## Requirements
<!-- 完整需求摘要，从需求文档提取 -->

### 状态机（6状态）
| 状态 | LED | 音频 | LCD |
|------|-----|------|-----|
| UNDEPLOYED | 全灭 | 无 | 无显示 |
| DEPLOYING | 全灭 | planting.mp3 (4.05s) | "正在部署" / "SPIKE PLANTING" |
| DEPLOYED | 红LED 6阶段闪烁 | planted.mp3 (45.04s) | 进度条(0%) + "爆能器已部署" / "SPIKE PLANTED" |
| DEFUSING | 红LED继续闪烁 + 绿LED常亮 | planted.mp3继续 + defuse_start PCM | 进度条(填充中) + "正在拆除" / "DEFUSING" |
| DEFUSED | 红灭 + 绿常亮 | planted停 → defused.mp3(2.98s) → 彩蛋顺序衔接 | 剩余时间xx.xx + "爆能器已拆除" + 切歌提示 |
| DETONATED | 红常亮 + 绿灭 | planted停 → boom.mp3(7.05s) → 彩蛋顺序衔接 | 时间(若拆除中) + "爆能器已启动" + 切歌提示 |

### 拆除进度逻辑
- <3.5s松手：进度归零，下次从头开始
- >=3.5s且<7s松手：进度保留50%，下次从50%继续（只需3.5s）
- >=7s持续：拆除成功

### 彩蛋逻辑
- 进入DEFUSED/DETONATED → 主音效(defused/boom)播完 → 随机播对应目录彩蛋
- KEY0切下一首，目录循环
- 播完不自动切，需手动按KEY0

### LED闪烁（45秒6阶段）
| 阶段 | 时间 | 亮(ms) | 灭(ms) | 周期(ms) | 频率 |
|------|------|--------|--------|-----------|------|
| 1 | 0-25s | 50 | 950 | 1000 | 1Hz |
| 2 | 25-35s | 50 | 450 | 500 | 2Hz |
| 3 | 35-40s | 50 | 200 | 250 | 4Hz |
| 4 | 40-42.5s | 50 | 75 | 125 | 8Hz |
| 5 | 42.5-44s | 25 | 25 | 50 | 20Hz |
| 6 | 44-45s | 12 | 13 | 25 | 40Hz |

## Research Findings

### 音频文件时长
| 文件 | 时长 | 用途 |
|------|------|------|
| planting.mp3 | 4.05s | 部署中 |
| planted.mp3 | 45.04s | 已部署(完美匹配45s) |
| defuse_start_1.mp3 | 0.84s | 从头开始拆除(<1s) |
| defuse_start_2.mp3 | 0.65s | 续拆已半进度(<1s) |
| defused.mp3 | 2.98s | 拆除成功 |
| boom.mp3 | 7.05s | 引爆 |
| Easter_eggs/defused/ (6个) | 1.38s~17.92s | 拆除后彩蛋 |
| Easter_eggs/detonated/ (4个) | 8.67s~15.49s | 引爆后彩蛋 |

### PCM音频参数
- defuse_start_1: 11025Hz 8-bit ≈ 9,261 bytes
- defuse_start_2: 11025Hz 8-bit ≈ 7,166 bytes
- 合计约16.4KB，外部SRAM (1MB) 完全足够
- 载波：TIM1_CH1 PA8, 281.25kHz PWM

### 硬件引脚确认（战舰V3 IO分配表）
| 功能 | 引脚 | 备注 |
|------|------|------|
| KEY_UP | PA0 | 下拉输入, 按下为高 |
| KEY0 | PE4 | 上拉输入, 按下为低 |
| KEY1 | PE3 | 上拉输入(未使用) |
| KEY2 | PE2 | 上拉输入(未使用) |
| 红LED(DS0) | PB5 | 低电平亮 |
| 绿LED(DS1) | PE5 | 低电平亮 |
| VS1053 SCK | PA5 | SPI1 |
| VS1053 MISO | PA6 | SPI1 |
| VS1053 MOSI | PA7 | SPI1 |
| VS1053 RST | PE6 | |
| VS1053 XDCS | PF6 | |
| VS1053 XCS | PF7 | |
| VS1053 DREQ | PC13 | |
| SDIO_D0~D3 | PC8~PC11 | |
| SDIO_SCK | PC12 | |
| SDIO_CMD | PD2 | |
| TFTLCD CS | PG12 | FSMC_NE4 |
| TFTLCD RS | PG0 | FSMC_A10 |
| TFTLCD BL | PB0 | 背光 |
| PWM音频 | PA8 | TIM1_CH1, P11 AIN-PDC短接 |

### 音频路径（最终方案）
```
[主音频]
SD卡 MP3 → VS1053解码(SPI1) → TDA1308T → PHONE口 → 外接音箱(3.5mm)

[短音效]
PCM数组 → TIM2 ISR(11025Hz) → TIM1 CCR1(PA8 PWM) → 
R77+C63 RC滤波 → P11(AIN-PDC短接) → TDA1308T+HT6872 → PHONE口 + 板载扬声器

[LINE_IN监听]（已配置但本项目不需要外部输入）
VS1053 SCI_MODE |= SM_LINE1 → LINE_IN输入混入主输出
```

## Technical Decisions
| Decision | Rationale |
|----------|-----------|
| PA8 PWM替代DAC | P11 AIN-PDC短接，走板载音频通路，零额外硬件 |
| TIM1 PWM 281.25kHz, TIM2 ISR 11025Hz | 参考PWM DAC实验，RC滤波器截止频率159kHz有效滤除载波 |
| GBK24字体(24px) | 用户指定大字体，480宽屏最宽文字192px轻松显示 |
| 非阻塞tick驱动 | SysTick 1ms + TIM2 11025Hz，所有延时基于tick差 |
| 彩蛋顺序衔接 | 用户明确选择方案A，主音效播完后再播彩蛋 |
| 板载扬声器+外接音箱双输出 | 两个物理扬声器同时发声，无需混音硬件 |
| 无软件重置 | 用户确认复位键即可 |

## Issues Encountered
| Issue | Resolution |
|-------|------------|
| .xlsx无法直接读取 | 创建Python venv安装pandas+openpyxl |
| conda不可用 | 改用Python内置venv |
| P11与P10混淆 | 查V4硬件手册：P10+P11=6针多功能端口，P11=AIN+PWM_AUDIO两针 |
| V4手册验证V3兼容性 | V3原理图同位置有相同P11+P10设计 |
| SD卡识别 | PowerShell Get-WmiObject找到G盘 |

## Resources
- 需求文档：`E:\课程内容\第四学期\嵌入式系统\大作业\嵌入式大作业.txt`
- IO分配表：`.../战舰V3 IO引脚分配表.xlsx`
- 原理图：`.../WarShip STM32F1_V3.4_SCH.pdf`
- V3音乐播放器：`.../实验43 音乐播放器实验`
- V3 PWM DAC：`.../实验21 PWM DAC实验`
- V4硬件手册：`.../战舰V4 硬件参考手册_V1.0.pdf`
- SD卡：G盘，已有 `/SOUNDS/` 和 `/SYSTEM/FONT/`
- 项目目录：`D:\STM32Projects\Valorant_Spike`
