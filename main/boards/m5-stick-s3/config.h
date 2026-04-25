#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// M5Stack StickS3 (ESP32-S3-PICO-1-N8R8, 8MB flash + 8MB PSRAM)
// https://docs.m5stack.com/en/core/StickS3

#include <driver/gpio.h>

// ─── 音频：ES8311 codec + AW8737 放大器 + MEMS 麦 ───────────────────
// ES8311 是单端 codec，输入输出共用一个采样率配置；必须相等
// 16k 匹配服务端 ASR/TTS（DASHSCOPE 的 16k mono PCM）
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

#define AUDIO_I2S_GPIO_MCLK  GPIO_NUM_18
#define AUDIO_I2S_GPIO_BCLK  GPIO_NUM_14
#define AUDIO_I2S_GPIO_WS    GPIO_NUM_17
#define AUDIO_I2S_GPIO_DIN   GPIO_NUM_16  // 麦 → ESP（ES8311 ASDOUT）
#define AUDIO_I2S_GPIO_DOUT  GPIO_NUM_15  // ESP → 扬声器（ES8311 DSDIN）

// I2C：codec / M5PM1 电源管理 / IMU 共用这一条总线
#define AUDIO_CODEC_I2C_SDA_PIN  GPIO_NUM_47
#define AUDIO_CODEC_I2C_SCL_PIN  GPIO_NUM_48
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR  // 0x18

// AW8737 放大器的使能/静音引脚：M5 官方文档没列出，怀疑是经 M5PM1 I/O 扩展
// 控制，不是直连 GPIO。先留 NC，实测扬声器静默再回来补 M5PM1 初始化
#define AUDIO_CODEC_PA_PIN       GPIO_NUM_NC

// ─── 按键 ─────────────────────────────────────────────────────────
// StickS3 两个物理按键：KEY1 (G11, 正面 M5 logo 下方) / KEY2 (G12, 右侧)
// BOOT 用 KEY1（短按切换对话状态；启动时长按进 wifi 配网）
#define BOOT_BUTTON_GPIO        GPIO_NUM_11
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_12
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC
#define BUILTIN_LED_GPIO        GPIO_NUM_NC

// ─── LCD：ST7789P3 135×240（1.14"），SPI 6 线 ────────────────────
// 取 portrait 方向（竖屏），面板可视区在 240x320 RAM 里 offset (52, 40)
#define DISPLAY_WIDTH   135
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_OFFSET_X 52
#define DISPLAY_OFFSET_Y 40

#define DISPLAY_SPI_SCLK_PIN    GPIO_NUM_40
#define DISPLAY_SPI_MOSI_PIN    GPIO_NUM_39
#define DISPLAY_SPI_CS_PIN      GPIO_NUM_41
#define DISPLAY_SPI_DC_PIN      GPIO_NUM_45
#define DISPLAY_SPI_RESET_PIN   GPIO_NUM_21
#define DISPLAY_SPI_SCLK_HZ     (40 * 1000 * 1000)

#define DISPLAY_BACKLIGHT_PIN            GPIO_NUM_38
// 实测 StickS3 的 BL 是低电平点亮（M5 板多数这样），要反过来
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT  true

#endif // _BOARD_CONFIG_H_
