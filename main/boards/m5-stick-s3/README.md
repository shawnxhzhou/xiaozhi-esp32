# M5Stack StickS3

ESP32-S3-PICO，1.14" 135×240 ST7789P3 LCD，ES8311 codec + AW8737 放大器 + MEMS 麦。

https://docs.m5stack.com/en/core/StickS3

## 编译

```bash
python ./scripts/release.py m5-stick-s3
```

或手动：`idf.py set-target esp32s3 && idf.py menuconfig`，在 `Board Type` 下选 `M5Stack StickS3`。

## 烧录

```bash
idf.py flash
```

**进入下载模式**：按住 `Reset` ~3 秒直到屏幕关闭，松开即可进入 USB 下载模式（和 AtomS3 同机制）。

## 按键

- `KEY1`（正面 M5 logo 下方，G11）：短按切换对话 / 中断 TTS；启动时长按进 WiFi 配网
- `KEY2`（右侧，G12）：音量 +（后续可改）

## 已知待办

- **AW8737 放大器使能引脚**：M5 官方文档未列出具体控制方式，猜测走 M5PM1 I/O 扩展。
  `AUDIO_CODEC_PA_PIN` 先置 `NC`，实测扬声器无声则需补 M5PM1 初始化（I2C 地址 `0x6e`）
- **M5PM1 电源管理**：电源按键、电池电量、各路电源使能都在这里。首版未接入
- **IMU (BMI270)**：暂未使用
- **红外 TX/RX**：暂未使用（Xiaozhi 的红外遥控 MCP 是阶段 4）

## 引脚速查

| 用途 | GPIO |
|---|---|
| I2S MCLK / BCLK / WS / DIN / DOUT | 18 / 14 / 17 / 16 / 15 |
| I2C SDA / SCL（codec + PM + IMU 共用） | 47 / 48 |
| LCD MOSI / SCK / CS / DC / RST / BL | 39 / 40 / 41 / 45 / 21 / 38 |
| 按键 KEY1 / KEY2 | 11 / 12 |
| IR TX / RX | 46 / 42 |
