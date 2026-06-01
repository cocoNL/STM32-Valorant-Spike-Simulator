# Task Plan: Valorant Spike 模拟器

## Goal
基于正点原子战舰V3（STM32F103ZET6），模拟Valorant游戏中"爆能器（Spike）"的部署、拆除、引爆全过程，含音频播放、LED闪烁、LCD显示及彩蛋系统。

## Current Phase
Phase 4 — 功能调试（已完成大部分核心功能）

## Phases

### Phase 1: 需求分析与参考学习
- [x] 完整阅读需求文档并充分理解
- [x] 明确引脚分配
- [x] 确定音频方案（全部音频VS1053→PHONE口，短音频暂停插入+时间同步恢复）
- [x] 将SOUNDS和SYSTEM目录部署到SD卡（G盘）
- [x] 获取所有MP3文件时长信息
- [x] 精读音乐播放器参考例程
- [x] 确认项目代码结构和初始化流程
- [x] P11 短接 AIN-PDC（已完成硬件配置）
- **Status:** complete

### Phase 2: 项目工程搭建
- [x] 2.1 参考音乐播放器例程搭建项目目录结构
- [x] 2.2 精简不需要的组件
- [x] 2.3 在Keil中配置工程（源文件分组、包含路径、ARM Compiler v5）
- [x] 2.4 修改main.c初始化流程适配本项目
- [x] 2.5 创建应用层源码（spike.c/h, spike_audio.c/h, pcm_data.c/h）
- [x] 2.6 配置PWM音频模块（PA8 TIM1_CH1）
- [x] 2.7 初始化Git仓库并推送GitHub
- [x] 2.8 命令行编译成功（0错误0警告）
- **Status:** complete

### Phase 3: 基础驱动适配
- [x] 3.1 LED驱动（PB5红色, PE5绿色）
- [x] 3.2 KEY驱动（KEY_UP部署, KEY1拆除, KEY0彩蛋）
- [x] 3.3 LCD驱动（4.3寸TFTLCD FSMC, GBK24字体）
- [x] 3.4 VS1053驱动（含硬件复位HD_Reset, LINE_IN监听）
- [x] 3.5 SD卡+FATFS（SDIO方式挂载）
- [x] 3.6 PWM音频输出（PA8 TIM1_CH1 + TIM2 ISR）
- [x] 3.7 HT6872功放控制（VS1053 GPIO4软件开关）
- **Status:** complete

### Phase 4: 核心功能调试
- [x] 4.1 状态机6状态转换 ✓
- [x] 4.2 部署流程（KEY_UP按住4s, 进度条, planting.mp3）✓
- [x] 4.3 拆除流程（KEY1按住7s/3.5s半进度保留, 进度条+中线）✓
- [x] 4.4 拆除成功（defused.mp3→彩蛋, +xx.xx s显示）✓
- [x] 4.5 引爆流程（boom.mp3→彩蛋, -xx.xx s显示）✓
- [x] 4.6 红LED 6阶段加速闪烁 ✓
- [x] 4.7 绿LED状态指示 ✓
- [x] 4.8 45秒倒计时（不可被拆除中断重置）✓
- [x] 4.9 主音频非阻塞喂数据（VS1053）✓
- [x] 4.10 短音频暂停/时间同步恢复 ✓
- [x] 4.11 彩蛋系统（随机+KEY0松手切歌+循环）✓
- [x] 4.12 LCD显示（各状态中英文+进度条+时间+按键提示）✓
- [x] 4.13 进度条100%即时跳转拆除成功 ✓
- [x] 4.14 拆除进度50%保留/不归零 ✓
- [ ] 4.15 引爆后仍需显示剩余拆除时间（拆除中被炸）— 待验证
- [ ] 4.16 板载扬声器vs耳机音频分离 — 硬件限制，当前可接受
- **Status:** in_progress

### Phase 5: 完善与交付
- [ ] 5.1 全部需求逐项对照验证
- [ ] 5.2 边界条件测试
- [ ] 5.3 清理无用代码（PCM/PWM/diagnostic）
- [ ] 5.4 最终版本烧录实测
- [ ] 5.5 整理交付
- **Status:** pending

## Decisions Made
| Decision | Rationale |
|----------|-----------|
| 音频：主MP3→PHONE, 短MP3暂停插入 | VS1053单解码器限制，暂停<1s不影响体验 |
| 按键：KEY_UP(部署), KEY1(拆除), KEY0(彩蛋) | 需求文档+用户指定 |
| 彩蛋KEY0松手切歌 | 防止误触，松开才切换 |
| 进度条100%即时跳转 | 不需等松手 |
| 50%进度保留不归零 | 已达标应保持，不应因后续失败丢失 |
| HT6872软件开关 | VS1053 GPIO4控制，只在短音频时打开 |
| 中文字符用GBK hex转义 | ARMCC v5兼容，避免编码问题 |
| GBK24字体 | 系统字库支持的最大字号 |

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
| VS1053未硬件复位导致DREQ死等 | 2 | spike_audio_init中加入VS_HD_Reset+VS_Soft_Reset |
| 按键检测抖动 | 3 | 改用KEY_Scan(1)+独立hold函数 |
| VS_Sine_Test后VS1053状态不干净 | 2 | 去除初始化阶段音频测试 |
| 快速重复按压planting无声 | 3 | 去除stop中的VS_Soft_Reset, 加DREQ超时等待 |
| 拆除时planted从板载泄露 | 2 | HT6872软件开关控制 |
| 进度条显示50%但被LCD刷新覆盖 | 1 | spike_lcd_update使用spike.defuse_progress |
| 拆除切换KEY1后反复跳动 | 1 | 添加key1_hold_ms独立追踪 |
| 彩蛋不播放(defused) | 1 | spike_audio_stop清resume_needed+重载egg目录 |
| 中文编码ARMCC不兼容 | 4 | 最终使用GBK hex转义序列 |

## Notes
- GitHub: https://github.com/cocoNL/STM32-Valorant-Spike-Simulator
- 最新commit: b54df04
- SD卡(G盘): SOUNDS + SYSTEM/FONT 已部署
- 耳机插PHONE口, 板载扬声器自动控制
- 外部供电: 12V 1A
