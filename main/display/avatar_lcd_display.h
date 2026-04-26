#ifndef AVATAR_LCD_DISPLAY_H
#define AVATAR_LCD_DISPLAY_H

// SpiLcdDisplay 的子类：在屏幕最上层挂一张全屏头像。
// SetStatus(SPEAKING) 启动 lip-flap 定时器（每 ~150ms 切张/闭嘴）；
// 其它正常状态显示闭嘴；OTA/配网等状态隐藏头像，露出底层 LVGL UI。
//
// 资源由 avatar_assets.c 提供（lv_img_dsc_t avatar_close / avatar_open）。

#include "lcd_display.h"

class AvatarLcdDisplay : public SpiLcdDisplay {
public:
    AvatarLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                     int width, int height, int offset_x, int offset_y,
                     bool mirror_x, bool mirror_y, bool swap_xy);
    ~AvatarLcdDisplay();

    void SetupUI() override;
    void SetStatus(const char* status) override;

private:
    lv_obj_t* avatar_img_ = nullptr;
    lv_timer_t* flap_timer_ = nullptr;
    bool showing_open_ = false;

    void ShowAvatar(bool show);
    void StartFlap();
    void StopFlap();
    static void FlapTickCb(lv_timer_t* t);
};

#endif  // AVATAR_LCD_DISPLAY_H
