#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "application.h"
#include "mcp_server.h"
#include "config.h"
#include "cw2017_battery.h"
#include "passport_display_rounding.h"
#include "passport_key_decoder.h"
#include "study_card_passport.h"

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <iot_button.h>
#include <cstdio>
#include <cstring>

#define TAG "AiPassportBoard"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);

namespace {

// ST7789P3 panel vendor initialization sequence from FoloToy/ai-passport bsp_display.c
struct St7789InitCmd {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;
    uint16_t delay_ms;
};

static const St7789InitCmd kSt7789p3Cmds[] = {
    {0xB2, {0x05, 0x05, 0x00, 0x33, 0x33}, 5, 0},
    {0xB7, {0x35}, 1, 0},
    {0xBB, {0x21}, 1, 0},
    {0xC0, {0x2C}, 1, 0},
    {0xC2, {0x01}, 1, 0},
    {0xC3, {0x0B}, 1, 0},
    {0xC4, {0x20}, 1, 0},
    {0xC6, {0x0F}, 1, 0},
    {0xD0, {0xA7, 0xA1}, 2, 0},
    {0xD0, {0xA4, 0xA1}, 2, 0},
    {0xD6, {0xA1}, 1, 0},
    {0xE0, {0xD0, 0x04, 0x08, 0x0A, 0x09, 0x05, 0x2D, 0x43,
            0x49, 0x09, 0x16, 0x15, 0x26, 0x2B}, 14, 0},
    {0xE1, {0xD0, 0x03, 0x09, 0x0A, 0x0A, 0x06, 0x2E, 0x44,
            0x40, 0x3A, 0x15, 0x15, 0x26, 0x2A}, 14, 10},
};

// Mask pixels outside the R=30 physical screen bezel right before SPI transfer
static void RoundedFlushEventCb(lv_event_t* event) {
    lv_display_t* disp = static_cast<lv_display_t*>(lv_event_get_target(event));
    const lv_area_t* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
    lv_draw_buf_t* draw_buf = lv_display_get_buf_active(disp);
    if (!area || !draw_buf || !draw_buf->data ||
        lv_display_get_color_format(disp) != LV_COLOR_FORMAT_RGB565) {
        return;
    }

    const int32_t width = lv_area_get_width(area);
    if (draw_buf->header.stride < static_cast<uint32_t>(width) * sizeof(uint16_t)) {
        return;
    }

    for (int32_t y = area->y1; y <= area->y2; ++y) {
        uint16_t* row = reinterpret_cast<uint16_t*>(
            draw_buf->data + (y - area->y1) * draw_buf->header.stride);
        int32_t visible_x1 = 0;
        int32_t visible_x2 = 0;
        if (!passport_display_rounded_row_span(y, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                               DISPLAY_SCREEN_RADIUS,
                                               &visible_x1, &visible_x2)) {
            std::memset(row, 0, static_cast<size_t>(width) * sizeof(uint16_t));
            continue;
        }
        const int32_t left_end = visible_x1 > area->x2 ? area->x2 : visible_x1 - 1;
        const int32_t right_start = visible_x2 < area->x1 ? area->x1 : visible_x2 + 1;
        for (int32_t x = area->x1; x <= left_end; ++x) {
            row[x - area->x1] = 0;
        }
        for (int32_t x = right_start; x <= area->x2; ++x) {
            row[x - area->x1] = 0;
        }
    }
}

class AiPassportDisplay : public SpiLcdDisplay {
public:
    AiPassportDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                      std::function<int()> battery_soc_provider,
                      std::function<bool()> ai_ready_provider)
        : SpiLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                        DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y,
                        DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY),
          battery_soc_provider_(std::move(battery_soc_provider)),
          ai_ready_provider_(std::move(ai_ready_provider)) {
        if (display_ && Lock(0)) {
            lv_display_add_event_cb(display_, RoundedFlushEventCb, LV_EVENT_FLUSH_START, nullptr);
            Unlock();
        }
    }

    void SetupUI() override {
        SpiLcdDisplay::SetupUI();
        // Keep xiaozhi top_bar / status_bar / bottom_bar clear of the R=30 rounded corners
        if (Lock(0)) {
            if (top_bar_) {
                lv_obj_set_style_pad_top(top_bar_, 6, 0);
                lv_obj_set_style_pad_left(top_bar_, 26, 0);
                lv_obj_set_style_pad_right(top_bar_, 26, 0);
            }
            if (status_bar_) {
                lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 6);
                lv_obj_set_style_pad_left(status_bar_, 26, 0);
                lv_obj_set_style_pad_right(status_bar_, 26, 0);
            }
            if (status_label_) {
                lv_obj_set_width(status_label_, 144);
            }
            if (notification_label_) {
                lv_obj_set_width(notification_label_, 168);
            }
            if (bottom_bar_) {
                lv_obj_align(bottom_bar_, LV_ALIGN_BOTTOM_MID, 0, -14);
                lv_obj_set_style_pad_left(bottom_bar_, 24, 0);
                lv_obj_set_style_pad_right(bottom_bar_, 24, 0);
                lv_obj_set_style_pad_top(bottom_bar_, 4, 0);
                lv_obj_set_style_pad_bottom(bottom_bar_, 4, 0);
            }
            if (chat_message_label_) {
                lv_obj_set_width(chat_message_label_, 188);
                lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_WRAP);
            }
            Unlock();
        }

        // IMPORTANT: Use the immortal static Flash BUILTIN_TEXT_FONT as fallback!
        // Never use current_theme_->text_font()->font(), which points to a heap-allocated
        // LvglBuiltInFont::font_ copy that gets freed when Assets::Apply() refreshes the theme.
        StudyCardPassport::GetInstance().Initialize(&BUILTIN_TEXT_FONT,
                                                    battery_soc_provider_,
                                                    ai_ready_provider_);
    }

private:
    std::function<int()> battery_soc_provider_;
    std::function<bool()> ai_ready_provider_;
};

// Shared 3-key ADC resistor ladder driver with key-lock debounce
struct PassportAdcBtnDriver {
    button_driver_t base;
    unsigned index;
};

adc_oneshot_unit_handle_t s_adc_unit = nullptr;
adc_cali_handle_t s_adc_cali = nullptr;
PassportKeyDecoder s_key_decoder;
int64_t s_last_sample_us = 0;
bool s_sample_valid = false;
PassportAdcBtnDriver s_btn_drivers[BSP_BTN_COUNT] = {};

uint8_t AdcLadderGetLevel(button_driver_t* driver) {
    if (!s_adc_unit || !s_adc_cali) {
        return BUTTON_INACTIVE;
    }
    const auto* btn = reinterpret_cast<const PassportAdcBtnDriver*>(driver);
    const int64_t now = esp_timer_get_time();
    if (!s_sample_valid || now - s_last_sample_us >= 1000) {
        s_last_sample_us = now;
        s_sample_valid = true;
        int sum = 0;
        bool ok = true;
        constexpr int kSamples = 4;
        for (int i = 0; i < kSamples; ++i) {
            int raw = 0;
            if (adc_oneshot_read(s_adc_unit, BSP_BTN_ADC_CHANNEL, &raw) != ESP_OK) {
                ok = false;
                break;
            }
            sum += raw;
        }
        int mv = -1;
        if (ok && adc_cali_raw_to_voltage(s_adc_cali, sum / kSamples, &mv) != ESP_OK) {
            mv = -1;
        }
        (void)s_key_decoder.Feed(PassportKeyDecoder::WindowOf(mv), now,
                                 BSP_BTN_CONFIRM_MS, BSP_BTN_HOLD_MS);
    }
    return s_key_decoder.locked == static_cast<int>(btn->index) ? BUTTON_ACTIVE : BUTTON_INACTIVE;
}

esp_err_t AdcLadderDelete(button_driver_t* driver) {
    (void)driver;
    return ESP_OK;
}

}  // namespace

class AiPassportBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_ = nullptr;
    Cw2017Battery battery_;
    AiPassportDisplay* display_ = nullptr;
    button_handle_t buttons_[BSP_BTN_COUNT] = {};

    void InitializeI2c() {
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port = BSP_I2C_PORT;
        cfg.sda_io_num = AUDIO_CODEC_I2C_SDA_PIN;
        cfg.scl_io_num = AUDIO_CODEC_I2C_SCL_PIN;
        cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt = 7;
        cfg.flags.enable_internal_pullup = 1;
        ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &i2c_bus_));
    }

    void InitializeBattery() {
        (void)battery_.Initialize(i2c_bus_, BSP_I2C_CW2017_ADDR);
    }

    void InitializeSpiAndDisplay() {
        // Release any leftover deep-sleep holds cleanly
        gpio_deep_sleep_hold_dis();

        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SPI_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SPI_SCK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * 40 * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        esp_lcd_panel_io_spi_config_t io_cfg = {};
        io_cfg.cs_gpio_num = DISPLAY_SPI_CS_PIN;
        io_cfg.dc_gpio_num = DISPLAY_DC_PIN;
        io_cfg.spi_mode = 0;  // ST7789P3 uses SPI mode 0 on AI Passport
        io_cfg.pclk_hz = 80 * 1000 * 1000;
        io_cfg.trans_queue_depth = 10;
        io_cfg.lcd_cmd_bits = 8;
        io_cfg.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_cfg, &panel_io));

        esp_lcd_panel_dev_config_t panel_cfg = {};
        panel_cfg.reset_gpio_num = DISPLAY_RST_PIN;  // -1 -> SWRESET
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_cfg, &panel));

        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
        for (const auto& cmd : kSt7789p3Cmds) {
            ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io, cmd.cmd, cmd.data, cmd.len));
            if (cmd.delay_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(cmd.delay_ms));
            }
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR));
        ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y));

        display_ = new AiPassportDisplay(
            panel_io, panel,
            [this]() { return battery_.ReadSoc(); },
            []() {
                auto st = Application::GetInstance().GetDeviceState();
                return st == kDeviceStateIdle || st == kDeviceStateListening ||
                       st == kDeviceStateSpeaking;
            });
    }

    void DispatchKeyEvent(PassportKey key, PassportKeyEvent ev) {
        // Always run on Application main loop (never touch LVGL/audio/network from timer CB)
        Application::GetInstance().Schedule([this, key, ev]() {
            auto& app = Application::GetInstance();
            StudyAction act = StudyCardPassport::GetInstance().HandleKey(key, ev);
            switch (act) {
                case StudyAction::kToggleVoiceChat:
                    app.ToggleChatState();
                    break;
                case StudyAction::kStartVoiceFollowUp:
                    app.ToggleChatState();
                    break;
                case StudyAction::kShowNetworkSetup:
                    if (display_) {
                        display_->ShowNotification("短按确定键返回识字", 3000);
                    }
                    break;
                case StudyAction::kEnterWifiConfig:
                    EnterWifiConfigMode();
                    break;
                case StudyAction::kVolumeUp: {
                    auto* codec = GetAudioCodec();
                    int vol = codec->output_volume() + 10;
                    if (vol > 100) vol = 100;
                    codec->SetOutputVolume(vol);
                    if (display_) {
                        char buf[24];
                        snprintf(buf, sizeof(buf), "音量 %d%%", vol);
                        display_->ShowNotification(buf, 1500);
                    }
                    break;
                }
                case StudyAction::kVolumeDown: {
                    auto* codec = GetAudioCodec();
                    int vol = codec->output_volume() - 10;
                    if (vol < 0) vol = 0;
                    codec->SetOutputVolume(vol);
                    if (display_) {
                        char buf[24];
                        snprintf(buf, sizeof(buf), "音量 %d%%", vol);
                        display_->ShowNotification(buf, 1500);
                    }
                    break;
                }
                case StudyAction::kCloseAndRewardStar:
                    if (display_) {
                        char buf[40];
                        snprintf(buf, sizeof(buf), "真棒！★%lu",
                                 (unsigned long)StudyCardPassport::GetInstance().GetStarCount());
                        display_->SetEmotion("happy");
                        display_->ShowNotification(buf, 2000);
                    }
                    break;
                case StudyAction::kNextCardWithStar:
                case StudyAction::kRenderView:
                case StudyAction::kCloseToPetHome:
                case StudyAction::kNone:
                default:
                    break;
            }
        });
    }

    struct BtnCallbackCtx {
        AiPassportBoard* board;
        PassportKey key;
        PassportKeyEvent ev;
    };

    static void OnIotButtonEvent(void* /*handle*/, void* usr_data) {
        auto* ctx = static_cast<BtnCallbackCtx*>(usr_data);
        if (ctx && ctx->board) {
            ctx->board->DispatchKeyEvent(ctx->key, ctx->ev);
        }
    }

    void InitializeButtons() {
        adc_oneshot_unit_init_cfg_t ucfg = {};
        ucfg.unit_id = BSP_BTN_ADC_UNIT;
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &s_adc_unit));

        adc_oneshot_chan_cfg_t ccfg = {};
        ccfg.atten = ADC_ATTEN_DB_12;
        ccfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_unit, BSP_BTN_ADC_CHANNEL, &ccfg));

        adc_cali_curve_fitting_config_t cal_cfg = {};
        cal_cfg.unit_id = BSP_BTN_ADC_UNIT;
        cal_cfg.chan = BSP_BTN_ADC_CHANNEL;
        cal_cfg.atten = ADC_ATTEN_DB_12;
        cal_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cal_cfg, &s_adc_cali));

        s_key_decoder.Reset();
        s_sample_valid = false;

        static BtnCallbackCtx ctxs[BSP_BTN_COUNT][3];
        const PassportKey keys[BSP_BTN_COUNT] = {
            PassportKey::kUp, PassportKey::kDown, PassportKey::kOk
        };
        const button_event_t evs[3] = {
            BUTTON_SINGLE_CLICK, BUTTON_DOUBLE_CLICK, BUTTON_LONG_PRESS_START
        };
        const PassportKeyEvent pevs[3] = {
            PassportKeyEvent::kClick, PassportKeyEvent::kDoubleClick, PassportKeyEvent::kLongPress
        };

        for (unsigned i = 0; i < BSP_BTN_COUNT; ++i) {
            s_btn_drivers[i].base.get_key_level = AdcLadderGetLevel;
            s_btn_drivers[i].base.del = AdcLadderDelete;
            s_btn_drivers[i].index = i;

            button_config_t bc = {
                .long_press_time = BSP_BTN_LONG_PRESS_MS,
                .short_press_time = BSP_BTN_SHORT_PRESS_MS,
            };
            ESP_ERROR_CHECK(iot_button_create(&bc, &s_btn_drivers[i].base, &buttons_[i]));
            for (int e = 0; e < 3; ++e) {
                ctxs[i][e] = {this, keys[i], pevs[e]};
                ESP_ERROR_CHECK(iot_button_register_cb(buttons_[i], evs[e], nullptr,
                                                       OnIotButtonEvent, &ctxs[i][e]));
            }
        }
        ESP_LOGI(TAG, "3-key ADC ladder initialized on GPIO0 (UP/DOWN/OK)");
    }

    void InitializeStudyTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool(
            "self.study.show",
            "在屏幕上大字展示小朋友问的汉字(楷体田字格)、词语或英文单词(四线三格)，方便孩子照着抄写和拼读。"
            "当小朋友问某个字/词怎么写、不认识某个字，或问某个英文单词怎么说、怎么拼时必须调用。"
            "参数: mode('hanzi'|'phrase'|'english'), text(核心字/词/英文单词), "
            "sub(带声调拼音或英文音标), hint(组词/部首/中文意思，简短)。",
            PropertyList({
                Property("mode", kPropertyTypeString, std::string("hanzi")),
                Property("text", kPropertyTypeString),
                Property("sub", kPropertyTypeString, std::string("")),
                Property("hint", kPropertyTypeString, std::string("")),
            }),
            [](const PropertyList& properties) -> ReturnValue {
                std::string mode = properties["mode"].value<std::string>();
                std::string text = properties["text"].value<std::string>();
                std::string sub = properties["sub"].value<std::string>();
                std::string hint = properties["hint"].value<std::string>();
                StudyCardPassport::GetInstance().Show(mode, text, sub, hint);
                return std::string("已在屏幕上展示: ") + text;
            });

        mcp.AddTool(
            "self.study.close",
            "关闭屏幕上的生字卡或单词卡，回到小宠物表情界面。当小朋友说写好了、记住了、关掉卡片时调用。",
            PropertyList(),
            [](const PropertyList& /*properties*/) -> ReturnValue {
                StudyCardPassport::GetInstance().Hide();
                return true;
            });

        mcp.AddTool(
            "self.pet.reward_star",
            "当小朋友答对问题、抄完生字或认真学习时调用，奖励一颗星星并展示开心表情。",
            PropertyList({
                Property("reason", kPropertyTypeString, std::string("认真学习")),
            }),
            [this](const PropertyList& properties) -> ReturnValue {
                std::string reason = properties["reason"].value<std::string>();
                uint32_t total = StudyCardPassport::GetInstance().RewardStar(reason);
                if (display_) {
                    char buf[48];
                    snprintf(buf, sizeof(buf), "获得小星星！累计 ★%lu", (unsigned long)total);
                    display_->SetEmotion("happy");
                    display_->ShowNotification(buf, 2500);
                }
                return std::string("已奖励1颗星星，当前总计 ") + std::to_string(total) + " 颗";
            });

        ESP_LOGI(TAG, "Registered study MCP tools: self.study.show, self.study.close, self.pet.reward_star");
    }

public:
    AiPassportBoard() {
        InitializeI2c();
        InitializeBattery();
        InitializeSpiAndDisplay();
        InitializeButtons();
        InitializeStudyTools();
        GetBacklight()->RestoreBrightness();
    }

    AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            i2c_bus_,
            BSP_I2C_PORT,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_PA_PIN,
            AUDIO_CODEC_ES8311_ADDR,
            /*use_mclk=*/true,
            /*pa_inverted=*/false,
            /*no_dac_ref=*/true);
        return &audio_codec;
    }

    Display* GetDisplay() override {
        return display_;
    }

    Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN,
                                      DISPLAY_BACKLIGHT_OUTPUT_INVERT,
                                      DISPLAY_BACKLIGHT_FREQ_HZ);
        return &backlight;
    }

    bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        int soc = battery_.ReadSoc();
        if (soc < 0) {
            return false;
        }
        level = soc;
        charging = false;
        discharging = true;
        return true;
    }
};

DECLARE_BOARD(AiPassportBoard);
