// M5Stack StickS3 板级适配
// 骨架参考 movecall-moji-esp32s3（同 ES8311 + SPI LCD 结构），
// LCD 初始化参考 zhengchen-1.54tft-ml307（ST7789 SPI 初始化模板）

#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <ssid_manager.h>

#define TAG "M5StickS3"

class M5StickS3Board : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_ = nullptr;
    i2c_master_dev_handle_t pm1_handle_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Button boot_button_;
    Button volume_up_button_;
    Display* display_ = nullptr;

    // codec / IMU / M5PM1 共用 I2C
    void InitializeCodecI2c() {
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    // 调试：扫一下 I2C 总线看谁响应。0x18=ES8311, 0x6e=M5PM1, 0x68=BMI270 IMU
    void ScanI2cBus() {
        ESP_LOGI(TAG, "=== I2C scan on G%d/G%d ===",
                 AUDIO_CODEC_I2C_SDA_PIN, AUDIO_CODEC_I2C_SCL_PIN);
        int found = 0;
        for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
            esp_err_t r = i2c_master_probe(codec_i2c_bus_, addr, 50);
            if (r == ESP_OK) {
                ESP_LOGI(TAG, "  found 0x%02x", addr);
                found++;
            }
        }
        ESP_LOGI(TAG, "=== %d devices ===", found);
    }

    // M5PM1（PY32L020F15U6 mcu）电源管理初始化。寄存器值参考 m5stack/M5Unified
    // src/M5Unified.cpp::board_M5StickS3 case + Power_Class.cpp。
    // 这一步不做的话：1) LCD 没电（屏幕黑） 2) AW8737 amp 没使能（扬声器不响）
    void InitializeM5PM1() {
        i2c_master_dev_handle_t pm1 = nullptr;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = M5PM1_I2C_ADDR,
            .scl_speed_hz = 100000,
        };
        ESP_ERROR_CHECK(i2c_master_bus_add_device(codec_i2c_bus_, &dev_cfg, &pm1));

        // 读改写 reg 的小工具
        auto read_reg = [&](uint8_t reg) -> uint8_t {
            uint8_t v = 0;
            ESP_ERROR_CHECK(i2c_master_transmit_receive(pm1, &reg, 1, &v, 1, 100));
            return v;
        };
        auto write_reg = [&](uint8_t reg, uint8_t v) {
            uint8_t buf[2] = {reg, v};
            ESP_ERROR_CHECK(i2c_master_transmit(pm1, buf, 2, 100));
        };

        // 关键！关掉 PMIC 的 I2C idle sleep（reg 0x09），否则 PMIC 会进睡眠不响应
        // 参考 M5GFX::M5GFX.cpp::Autodetect_StickS3
        write_reg(0x09, 0x00);
        ESP_LOGI(TAG, "M5PM1 reg 0x09 I2C idle sleep disabled");

        // 1) PM1 GPIO2 → L3B Enable，**点亮 LCD 电源**（之前漏了这个，所以屏幕一直黑）
        write_reg(0x16, read_reg(0x16) & ~(1 << 2));  // GPIO2 为 GPIO 功能
        write_reg(0x10, read_reg(0x10) |  (1 << 2));  // GPIO2 设为输出
        write_reg(0x13, read_reg(0x13) & ~(1 << 2));  // GPIO2 push-pull
        write_reg(0x11, read_reg(0x11) |  (1 << 2));  // GPIO2 输出 HIGH → 打开 L3B 轨
        ESP_LOGI(TAG, "M5PM1 GPIO2 (L3B/LCD power) ENABLED");

        // 2) 打开 5V boost (reg 0x06 bit3) —— 给 AW8737 amp 输出供 5V
        uint8_t v06 = read_reg(0x06);
        write_reg(0x06, v06 | 0x08);
        ESP_LOGI(TAG, "M5PM1 reg 0x06 5V_OUT enabled: %02x -> %02x", v06, v06 | 0x08);

        // 3) PM1 GPIO3 → AW8737 (扬声器 amp) 使能脚
        write_reg(0x16, read_reg(0x16) & ~(1 << 3));  // GPIO3 为 GPIO 功能
        write_reg(0x10, read_reg(0x10) |  (1 << 3));  // GPIO3 设为输出
        write_reg(0x13, read_reg(0x13) & ~(1 << 3));  // GPIO3 push-pull
        write_reg(0x11, read_reg(0x11) & ~(1 << 3));  // GPIO3 先输出低（amp 关闭，等屏幕亮起再开）
        ESP_LOGI(TAG, "M5PM1 GPIO3 (AW8737 ctrl) configured (amp off for now)");

        vTaskDelay(pdMS_TO_TICKS(100));  // 等 LCD 电源稳定 + codec 唤醒

        // 留着 dev handle 备用，但先不删（后面如果要打开 amp 还要写 0x11 bit3=1）
        // i2c_master_bus_rm_device(pm1);
        pm1_handle_ = pm1;
    }

    void M5PM1_SetAmpEnabled(bool on) {
        if (pm1_handle_ == nullptr) return;
        uint8_t reg = 0x11, v;
        ESP_ERROR_CHECK(i2c_master_transmit_receive(pm1_handle_, &reg, 1, &v, 1, 100));
        if (on) v |= (1 << 3); else v &= ~(1 << 3);
        uint8_t buf[2] = {0x11, v};
        ESP_ERROR_CHECK(i2c_master_transmit(pm1_handle_, buf, 2, 100));
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    // ST7789P3 初始化。1.14" 135×240 面板的显示区在 240×320 RAM 里是
    // offset (52, 40)，需要传给 SpiLcdDisplay 裁剪。颜色要 invert，不然
    // M5 的屏会出现负片效果。
    void InitializeSt7789Display() {
        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_SPI_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = DISPLAY_SPI_SCLK_HZ;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install ST7789 panel driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_SPI_RESET_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_));
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, true));
        ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_, DISPLAY_SWAP_XY));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new SpiLcdDisplay(panel_io_, panel_,
                                     DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                     DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                                     DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y,
                                     DISPLAY_SWAP_XY);
    }

    // 首次启动（NVS 里没有保存的 SSID）时种入预置的 WiFi 列表，
    // WifiManager 会在所有保存的 SSID 中扫描连接最强信号的那个。
    // 用户后续通过 AP 配网模式新加的 SSID 会和这些一起留在 NVS 里。
    void SeedHardcodedSsids() {
        auto& mgr = SsidManager::GetInstance();
        if (!mgr.GetSsidList().empty()) {
            ESP_LOGI(TAG, "NVS already has %zu SSIDs, skip seeding", mgr.GetSsidList().size());
            return;
        }
        ESP_LOGI(TAG, "First boot: seeding hardcoded WiFi list");
        mgr.AddSsid("SHAWN", "SS03010301");
        mgr.AddSsid("iPhone Air Shawn", "12345667");
        mgr.AddSsid("WiFi", "12345678");
    }

    void InitializeButtons() {
        // KEY1 (G11)：短按切对话状态；启动时长按进 wifi 配网
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // KEY2 (G12)：单击循环 5 档音量（30→50→70→85→100→30...）
        // 长按：直接调到 100（紧急放大）
        volume_up_button_.OnClick([this]() {
            auto codec = GetAudioCodec();
            if (codec == nullptr) return;
            static const int LEVELS[] = {30, 50, 70, 85, 100};
            int cur = codec->output_volume();
            int next = LEVELS[0];  // 默认从最低开始
            for (int i = 0; i < 5; i++) {
                if (cur < LEVELS[i]) {
                    next = LEVELS[i];
                    break;
                }
                if (i == 4) next = LEVELS[0];  // cur==100 → 回到 30
            }
            codec->SetOutputVolume(next);
            char msg[32];
            snprintf(msg, sizeof(msg), "音量 %d%%", next);
            if (auto d = GetDisplay()) d->ShowNotification(msg, 1500);
        });
        volume_up_button_.OnLongPress([this]() {
            auto codec = GetAudioCodec();
            if (codec == nullptr) return;
            codec->SetOutputVolume(100);
            if (auto d = GetDisplay()) d->ShowNotification("音量 100%", 1500);
        });
    }

public:
    M5StickS3Board()
        : boot_button_(BOOT_BUTTON_GPIO),
          volume_up_button_(VOLUME_UP_BUTTON_GPIO) {
        InitializeCodecI2c();
        ScanI2cBus();               // 诊断：先扫一下总线
        InitializeM5PM1();          // 必须在 LCD/Codec 之前：打开 5V，AW8737 amp 准备
        vTaskDelay(pdMS_TO_TICKS(200));  // 等电源稳定
        ScanI2cBus();               // 再扫一次：看 PM1 init 后多了谁
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        SeedHardcodedSsids();
        GetBacklight()->RestoreBrightness();
        M5PM1_SetAmpEnabled(true);  // 屏幕亮起来之后，把 amp 打开
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);  // NC → 静默不工作
        return &led;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            codec_i2c_bus_, I2C_NUM_0,
            AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }
};

DECLARE_BOARD(M5StickS3Board);
