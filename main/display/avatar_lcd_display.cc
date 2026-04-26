#include "avatar_lcd_display.h"
#include "assets/lang_config.h"
#include "board.h"
#include "backlight.h"

#include <esp_log.h>
#include <cstdio>
#include <cstring>

extern "C" {
    extern const lv_img_dsc_t avatar_close;
    extern const lv_img_dsc_t avatar_open;
}

namespace {
constexpr const char* TAG = "AvatarDisplay";
// 进入 SPEAKING 后等多久才开始动嘴：避开 TTS 首包前的静默/连接延迟
constexpr uint32_t SPEAK_DELAY_MS = 2000;
// 张/闭嘴切换平均周期；运行时再叠 ±60ms 抖动让节奏更自然
constexpr uint32_t FLAP_PERIOD_MS = 300;
constexpr uint32_t FLAP_JITTER_MS = 60;
// 省电：待机暗、对话亮（PwmBacklight 0–100）
constexpr uint8_t  BRIGHTNESS_IDLE   = 35;
constexpr uint8_t  BRIGHTNESS_ACTIVE = 85;
}

AvatarLcdDisplay::AvatarLcdDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel,
                                   int width, int height,
                                   int offset_x, int offset_y,
                                   bool mirror_x, bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y,
                    mirror_x, mirror_y, swap_xy) {}

AvatarLcdDisplay::~AvatarLcdDisplay() {
    StopFlap();
}

void AvatarLcdDisplay::SetupUI() {
    SpiLcdDisplay::SetupUI();  // 让父类先把 status_bar_/notification_label_ 等建好

    DisplayLockGuard lock(this);
    lv_obj_t* screen = lv_screen_active();

    // 头像：全屏铺底
    avatar_img_ = lv_image_create(screen);
    lv_image_set_src(avatar_img_, &avatar_close);
    lv_obj_set_pos(avatar_img_, 0, 0);
    lv_obj_set_size(avatar_img_, LV_HOR_RES, LV_VER_RES);
    lv_obj_add_flag(avatar_img_, LV_OBJ_FLAG_HIDDEN);

    // 底部状态/字幕条：半透明黑底，盖在头像上层但不遮嘴部（嘴在 0.65–0.72，条在底部 56px）
    constexpr int STRIP_H = 56;
    bottom_strip_ = lv_obj_create(screen);
    lv_obj_set_size(bottom_strip_, LV_HOR_RES, STRIP_H);
    lv_obj_align(bottom_strip_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(bottom_strip_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bottom_strip_, LV_OPA_70, 0);
    lv_obj_set_style_border_width(bottom_strip_, 0, 0);
    lv_obj_set_style_radius(bottom_strip_, 0, 0);
    lv_obj_set_style_pad_all(bottom_strip_, 3, 0);
    lv_obj_remove_flag(bottom_strip_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bottom_strip_, LV_OBJ_FLAG_HIDDEN);

    avatar_status_label_ = lv_label_create(bottom_strip_);
    lv_obj_set_style_text_color(avatar_status_label_, lv_color_white(), 0);
    lv_obj_align(avatar_status_label_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(avatar_status_label_, "");

    avatar_chat_label_ = lv_label_create(bottom_strip_);
    lv_obj_set_style_text_color(avatar_chat_label_, lv_color_white(), 0);
    lv_label_set_long_mode(avatar_chat_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(avatar_chat_label_, LV_HOR_RES - 6);
    lv_obj_align(avatar_chat_label_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_label_set_text(avatar_chat_label_, "");

    // 右上角电量小标签：白字、半透明黑底，圆角小药丸
    avatar_battery_label_ = lv_label_create(screen);
    lv_obj_set_style_text_color(avatar_battery_label_, lv_color_white(), 0);
    lv_obj_set_style_bg_color(avatar_battery_label_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(avatar_battery_label_, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(avatar_battery_label_, 4, 0);
    lv_obj_set_style_pad_ver(avatar_battery_label_, 1, 0);
    lv_obj_set_style_radius(avatar_battery_label_, 6, 0);
    lv_obj_align(avatar_battery_label_, LV_ALIGN_TOP_RIGHT, -3, 3);
    lv_label_set_text(avatar_battery_label_, "");
    lv_obj_add_flag(avatar_battery_label_, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "avatar overlay created (%d x %d) + bottom strip %dpx",
             LV_HOR_RES, LV_VER_RES, STRIP_H);
}

void AvatarLcdDisplay::UpdateStatusBar(bool update_all) {
    SpiLcdDisplay::UpdateStatusBar(update_all);

    int level = -1;
    bool charging = false, discharging = false;
    if (!Board::GetInstance().GetBatteryLevel(level, charging, discharging)) {
        return;
    }

    DisplayLockGuard lock(this);
    if (avatar_battery_label_ == nullptr) return;
    char buf[16];
    // 充电时前面加 "+"，明显区分
    if (charging) snprintf(buf, sizeof(buf), "+%d%%", level);
    else          snprintf(buf, sizeof(buf), "%d%%", level);
    lv_label_set_text(avatar_battery_label_, buf);
}

void AvatarLcdDisplay::SetChatMessage(const char* role, const char* content) {
    SpiLcdDisplay::SetChatMessage(role, content);
    if (content == nullptr) return;
    DisplayLockGuard lock(this);
    if (avatar_chat_label_ != nullptr) {
        lv_label_set_text(avatar_chat_label_, content);
    }
}

void AvatarLcdDisplay::ShowAvatar(bool show) {
    if (avatar_img_ == nullptr) return;
    if (show) {
        lv_obj_remove_flag(avatar_img_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(avatar_img_);
        if (bottom_strip_ != nullptr) {
            lv_obj_remove_flag(bottom_strip_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(bottom_strip_);  // 字幕条压在头像之上
        }
        if (avatar_battery_label_ != nullptr) {
            lv_obj_remove_flag(avatar_battery_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(avatar_battery_label_);
        }
    } else {
        lv_obj_add_flag(avatar_img_, LV_OBJ_FLAG_HIDDEN);
        if (bottom_strip_ != nullptr) {
            lv_obj_add_flag(bottom_strip_, LV_OBJ_FLAG_HIDDEN);
        }
        if (avatar_battery_label_ != nullptr) {
            lv_obj_add_flag(avatar_battery_label_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void AvatarLcdDisplay::SetStatus(const char* status) {
    SpiLcdDisplay::SetStatus(status);  // 状态文字仍交给父类（即便被遮挡，用作日志）

    if (status == nullptr) return;

    const bool is_idle      = std::strcmp(status, Lang::Strings::STANDBY)   == 0;
    const bool is_listening = std::strcmp(status, Lang::Strings::LISTENING) == 0;
    const bool is_speaking  = std::strcmp(status, Lang::Strings::SPEAKING)  == 0;

    DisplayLockGuard lock(this);
    if (is_idle || is_listening || is_speaking) {
        ShowAvatar(true);
        if (avatar_status_label_ != nullptr) {
            lv_label_set_text(avatar_status_label_, status);
        }
    } else {
        ShowAvatar(false);  // 配网/OTA/激活等场景露出底层 UI
    }

    // 省电：聊天中屏幕拉亮，待机调暗
    auto* bl = Board::GetInstance().GetBacklight();
    if (bl != nullptr) {
        if (is_listening || is_speaking) bl->SetBrightness(BRIGHTNESS_ACTIVE);
        else if (is_idle)                bl->SetBrightness(BRIGHTNESS_IDLE);
    }

    if (is_speaking) ScheduleFlap();
    else             StopFlap();
}

void AvatarLcdDisplay::ScheduleFlap() {
    // 已经在 flap 或已经在等了：不动
    if (flap_timer_ != nullptr || delay_timer_ != nullptr) return;
    delay_timer_ = lv_timer_create(&AvatarLcdDisplay::DelayDoneCb, SPEAK_DELAY_MS, this);
    lv_timer_set_repeat_count(delay_timer_, 1);  // one-shot
    ESP_LOGI(TAG, "speaking; flap in %u ms", (unsigned)SPEAK_DELAY_MS);
}

void AvatarLcdDisplay::DelayDoneCb(lv_timer_t* t) {
    auto self = static_cast<AvatarLcdDisplay*>(lv_timer_get_user_data(t));
    if (!self) return;
    self->delay_timer_ = nullptr;  // LVGL 会自动 free 一次性 timer
    self->StartFlap();
}

void AvatarLcdDisplay::StartFlap() {
    if (flap_timer_ != nullptr) return;
    showing_open_ = false;
    flap_timer_ = lv_timer_create(&AvatarLcdDisplay::FlapTickCb, FLAP_PERIOD_MS, this);
    ESP_LOGI(TAG, "flap start");
}

void AvatarLcdDisplay::StopFlap() {
    if (delay_timer_ != nullptr) {
        lv_timer_del(delay_timer_);
        delay_timer_ = nullptr;
    }
    if (flap_timer_ != nullptr) {
        lv_timer_del(flap_timer_);
        flap_timer_ = nullptr;
        ESP_LOGI(TAG, "flap stop");
    }
    if (avatar_img_ != nullptr) {
        lv_image_set_src(avatar_img_, &avatar_close);
        showing_open_ = false;
    }
}

void AvatarLcdDisplay::FlapTickCb(lv_timer_t* t) {
    auto self = static_cast<AvatarLcdDisplay*>(lv_timer_get_user_data(t));
    if (!self || self->avatar_img_ == nullptr) return;
    self->showing_open_ = !self->showing_open_;
    lv_image_set_src(self->avatar_img_,
                     self->showing_open_ ? &avatar_open : &avatar_close);
    // FLAP_PERIOD_MS ± FLAP_JITTER_MS：节奏抖一抖更自然
    uint32_t jitter = (esp_log_timestamp() % (FLAP_JITTER_MS * 2));
    uint32_t base = FLAP_PERIOD_MS - FLAP_JITTER_MS;
    lv_timer_set_period(t, base + jitter);
}
