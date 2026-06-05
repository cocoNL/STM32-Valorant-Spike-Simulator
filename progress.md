# Progress Log

## Session: 2026-05-31 ~ 2026-06-01

### Phase 1-2: 需求分析与工程搭建
- **Status:** complete
- **Completed:** 2026-05-31

### Phase 3: 基础驱动适配
- **Status:** complete
- **Completed:** 2026-06-01

### Phase 4: 核心功能调试
- **Status:** in_progress
- **Started:** 2026-06-01

#### 已修复的Bug
1. **VS1053 DREQ死等** — 添加VS_HD_Reset硬件复位
2. **按键检测失效** — 改用KEY_Scan(1)+独立hold函数
3. **初始化音频测试污染VS1053** — 去除VS_Sine_Test和PCM测试
4. **快速按压无planting音效** — 去除stop中的VS_Soft_Reset, 加DREQ超时
5. **板载扬声器泄露planted** — HT6872 GPIO4软件开关
6. **进度条保留50%被LCD刷新覆盖** — spike_lcd_update修正
7. **KEY1拆除反复跳动** — 添加key1_hold_ms独立追踪
8. **彩蛋不播放(defused)** — resume_needed清零+重载egg目录
9. **中文编码ARMCC不兼容** — GBK hex转义序列
10. **音频卡顿** — 去除32byte/次限制, 倾力灌数据
11. **倒计时被拆除退回重置** — countdown_start_ms非零检查
12. **部署进度条** — 新增无中线进度条
13. **拆除即时成功** — 100%时不等松手
14. **KEY0松手切歌** — 改为释放检测
15. **时间格式** — + 00.00 s / - 00.00 s
16. **清理无用测试代码** — 删除led_on/led_last_toggle/deploy_press_ms
17. **进度条对比度** — 黑框+红填充+白底
18. **彩蛋播放方式** — defused/boom播完后不自动播，等KEY0松开才播
19. **彩蛋切换闪烁** — 底部文字只更新一次
20. **部署<1s松手** — 屏幕立即回未部署，音频延后到1s停止
21. **部署>=1s松手** — 音频立即停止

- 项目参考音乐播放器例程的目录结构和驱动代码搭建
- SD卡已部署SOUNDS和SYSTEM/FONT文件

#### 已完成功能
- 6状态完整流转
- 部署4s+进度条
- 拆除7s(3.5s半进度保留)
- 红LED 6阶段加速闪烁
- 绿LED状态指示
- 45秒倒计时
- VS1053 MP3播放（非阻塞）
- 短音频暂停/时间同步恢复
- 彩蛋随机+KEY0循环
- LCD全状态显示（中英文+进度条+时间+按键提示）

#### 当前音频方案
- 全部MP3音频 → VS1053 → TDA1308T → PHONE口(外接耳机/音箱)
- 短音频(defuse_start) → 短暂暂停planted → VS1053播放 → 时间同步恢复
- 板载扬声器不参与音频播放(HT6872保持关闭)

#### 按键分配
- KEY_UP(PA0) — 部署
- KEY1(PE3) — 拆除
- KEY0(PE4) — 彩蛋切歌

#### Git记录
- 最新: b54df04 — Time display format + key hints
- 仓库: https://github.com/cocoNL/STM32-Valorant-Spike-Simulator
