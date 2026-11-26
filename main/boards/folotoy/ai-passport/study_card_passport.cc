#include "study_card_passport.h"
#include "study_book_nvs.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <cstdio>

#define TAG "StudyCardPassport"

namespace {

constexpr int32_t kScreenW = 240;
constexpr int32_t kScreenH = 320;
// R=30 rounded bezel safe horizontal padding for top/bottom bars
constexpr int32_t kCornerSafeX = 28;
constexpr int32_t kSafeW = kScreenW - kCornerSafeX * 2;  // 184 px
constexpr uint64_t kAutoHideUs = 180ULL * 1000000ULL;    // 3 minutes stay-on-screen

}  // namespace

StudyCardPassport::~StudyCardPassport() {
    if (auto_hide_timer_) {
        esp_timer_stop(auto_hide_timer_);
        esp_timer_delete(auto_hide_timer_);
        auto_hide_timer_ = nullptr;
    }
}

void StudyCardPassport::Initialize(const lv_font_t* builtin_fallback_font,
                                   std::function<int()> battery_soc_provider,
                                   std::function<bool()> ai_ready_provider) {
    if (initialized_) {
        return;
    }
    battery_soc_provider_ = std::move(battery_soc_provider);
    ai_ready_provider_ = std::move(ai_ready_provider);
    fonts_.Initialize(builtin_fallback_font);

    std::vector<StudyItem> saved_items;
    uint32_t saved_stars = 0;
    if (StudyBookNvs::Load(saved_items, saved_stars)) {
        sm_.LoadSaved(std::move(saved_items), saved_stars);
    }
    // Open StudyCard immediately on boot so the user can browse, zoom, and practice
    // starter/saved cards right away instead of getting blocked on Wi-Fi or pairing screens.
    sm_.OpenStudyCard();

    esp_timer_create_args_t timer_args = {
        .callback = OnAutoHideTimer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "passport_study_tmr",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&timer_args, &auto_hide_timer_);

    if (lvgl_port_lock(0)) {
        CreateOverlayLocked();
        RenderCurrentViewLocked();
        lvgl_port_unlock();
    }

    initialized_ = true;
    ESP_LOGI(TAG, "StudyCardPassport initialized (fonts=%d, items=%u, stars=%lu)",
             fonts_.IsReady(), (unsigned)sm_.items().size(), (unsigned long)sm_.star_count());
}

void StudyCardPassport::CreateOverlayLocked() {
    lv_obj_t* top = lv_layer_top();
    const lv_font_t* ui_font = fonts_.ui16();

    overlay_ = lv_obj_create(top);
    lv_obj_set_size(overlay_, kScreenW, kScreenH);
    lv_obj_set_pos(overlay_, 0, 0);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0xFDF8EC), 0);  // Warm eye-care paper
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(overlay_, 0, 0);
    lv_obj_set_style_radius(overlay_, 0, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);
    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);

    // Top bar (y=8..28, strictly within R=30 safe horizontal zone [28..212])
    top_star_lbl_ = lv_label_create(overlay_);
    lv_obj_set_style_text_font(top_star_lbl_, ui_font, 0);
    lv_obj_set_style_text_color(top_star_lbl_, lv_color_hex(0xD97706), 0);
    lv_obj_set_pos(top_star_lbl_, kCornerSafeX, 8);
    lv_label_set_text(top_star_lbl_, "★0");

    top_page_lbl_ = lv_label_create(overlay_);
    lv_obj_set_style_text_font(top_page_lbl_, ui_font, 0);
    lv_obj_set_style_text_color(top_page_lbl_, lv_color_hex(0x64748B), 0);
    lv_obj_set_width(top_page_lbl_, 80);
    lv_obj_set_style_text_align(top_page_lbl_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(top_page_lbl_, (kScreenW - 80) / 2, 8);
    lv_label_set_text(top_page_lbl_, "1/12");

    top_batt_lbl_ = lv_label_create(overlay_);
    lv_obj_set_style_text_font(top_batt_lbl_, ui_font, 0);
    lv_obj_set_style_text_color(top_batt_lbl_, lv_color_hex(0x475569), 0);
    lv_obj_set_width(top_batt_lbl_, 54);
    lv_obj_set_style_text_align(top_batt_lbl_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(top_batt_lbl_, kScreenW - kCornerSafeX - 54, 8);
    lv_label_set_text(top_batt_lbl_, "--");

    // Subtle top divider
    lv_obj_t* top_div = lv_obj_create(overlay_);
    lv_obj_set_size(top_div, kSafeW, 1);
    lv_obj_set_pos(top_div, kCornerSafeX, 29);
    lv_obj_set_style_bg_color(top_div, lv_color_hex(0xE5DEC9), 0);
    lv_obj_set_style_border_width(top_div, 0, 0);

    // Dynamic center area (y=30..286, height=256)
    content_area_ = lv_obj_create(overlay_);
    lv_obj_set_size(content_area_, kScreenW, 256);
    lv_obj_set_pos(content_area_, 0, 30);
    lv_obj_set_style_bg_opa(content_area_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content_area_, 0, 0);
    lv_obj_set_style_pad_all(content_area_, 0, 0);
    lv_obj_clear_flag(content_area_, LV_OBJ_FLAG_SCROLLABLE);

    // Subtle bottom divider
    lv_obj_t* bot_div = lv_obj_create(overlay_);
    lv_obj_set_size(bot_div, kSafeW, 1);
    lv_obj_set_pos(bot_div, kCornerSafeX, 287);
    lv_obj_set_style_bg_color(bot_div, lv_color_hex(0xE5DEC9), 0);
    lv_obj_set_style_border_width(bot_div, 0, 0);

    // Bottom 3-key legend (y=292..312, within R=30 safe zone)
    footer_lbl_ = lv_label_create(overlay_);
    lv_obj_set_style_text_font(footer_lbl_, ui_font, 0);
    lv_obj_set_style_text_color(footer_lbl_, lv_color_hex(0x64748B), 0);
    lv_obj_set_width(footer_lbl_, kSafeW);
    lv_obj_set_style_text_align(footer_lbl_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(footer_lbl_, kCornerSafeX, 293);
    lv_label_set_text(footer_lbl_, "▲▼换字 ●写好 长按AI");
}

lv_obj_t* StudyCardPassport::CreateTianzigeBox(lv_obj_t* parent, int32_t size,
                                               lv_point_precise_t* h_pts,
                                               lv_point_precise_t* v_pts) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, size, size);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xFFFDF8), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0xDC2626), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    int32_t inner = size - 4;
    int32_t mid = inner / 2;

    h_pts[0] = {0, mid};
    h_pts[1] = {inner, mid};
    lv_obj_t* hline = lv_line_create(box);
    lv_line_set_points(hline, h_pts, 2);
    lv_obj_set_style_line_color(hline, lv_color_hex(0xFCA5A5), 0);
    lv_obj_set_style_line_width(hline, 1, 0);
    lv_obj_set_style_line_dash_width(hline, 5, 0);
    lv_obj_set_style_line_dash_gap(hline, 4, 0);

    v_pts[0] = {mid, 0};
    v_pts[1] = {mid, inner};
    lv_obj_t* vline = lv_line_create(box);
    lv_line_set_points(vline, v_pts, 2);
    lv_obj_set_style_line_color(vline, lv_color_hex(0xFCA5A5), 0);
    lv_obj_set_style_line_width(vline, 1, 0);
    lv_obj_set_style_line_dash_width(vline, 5, 0);
    lv_obj_set_style_line_dash_gap(vline, 4, 0);

    return box;
}

void StudyCardPassport::RenderTopBarLocked(bool flash_star) {
    char buf[32];
    if (flash_star) {
        snprintf(buf, sizeof(buf), "★%lu+1", (unsigned long)sm_.star_count());
        lv_obj_set_style_text_color(top_star_lbl_, lv_color_hex(0xEA580C), 0);
    } else {
        snprintf(buf, sizeof(buf), "★%lu", (unsigned long)sm_.star_count());
        lv_obj_set_style_text_color(top_star_lbl_, lv_color_hex(0xD97706), 0);
    }
    lv_label_set_text(top_star_lbl_, buf);

    if (sm_.view() == StudyView::kWordbook) {
        snprintf(buf, sizeof(buf), "生字本 %d", (int)sm_.items().size());
    } else if (!sm_.items().empty()) {
        snprintf(buf, sizeof(buf), "%d/%d", sm_.cursor() + 1, (int)sm_.items().size());
    } else {
        snprintf(buf, sizeof(buf), "0/0");
    }
    lv_label_set_text(top_page_lbl_, buf);

    int soc = battery_soc_provider_ ? battery_soc_provider_() : -1;
    if (soc >= 0 && soc <= 100) {
        snprintf(buf, sizeof(buf), "%d%%", soc);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(top_batt_lbl_, buf);
}

void StudyCardPassport::RenderHanziLocked(const StudyItem& item, bool zoomed) {
    if (!item.sub.empty()) {
        lv_obj_t* py_lbl = lv_label_create(content_area_);
        lv_obj_set_style_text_font(py_lbl, fonts_.ui16(), 0);
        lv_obj_set_style_text_color(py_lbl, lv_color_hex(0x1D4ED8), 0);
        lv_obj_set_width(py_lbl, kScreenW - 24);
        lv_obj_set_style_text_align(py_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(py_lbl, 12, 6);
        lv_label_set_text(py_lbl, item.sub.c_str());
    }

    constexpr int32_t kBoxSize = 144;
    int32_t box_y = item.sub.empty() ? 14 : 30;
    lv_obj_t* box = CreateTianzigeBox(content_area_, kBoxSize, grid_h_pts_[0], grid_v_pts_[0]);
    lv_obj_set_pos(box, (kScreenW - kBoxSize) / 2, box_y);

    lv_obj_t* ch_lbl = lv_label_create(box);
    lv_obj_set_style_text_font(ch_lbl, fonts_.kai76(), 0);
    lv_obj_set_style_text_color(ch_lbl, lv_color_hex(0x1E293B), 0);
    lv_label_set_text(ch_lbl, item.text.c_str());
    lv_obj_center(ch_lbl);

    if (!item.hint.empty()) {
        lv_obj_t* hint_lbl = lv_label_create(content_area_);
        lv_obj_set_style_text_font(hint_lbl, fonts_.ui16(), 0);
        lv_obj_set_style_text_color(hint_lbl, lv_color_hex(0x334155), 0);
        lv_obj_set_width(hint_lbl, kScreenW - 24);
        lv_obj_set_style_text_align(hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(hint_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(hint_lbl, 4, 0);
        lv_obj_set_pos(hint_lbl, 12, box_y + kBoxSize + 10);
        lv_label_set_text(hint_lbl, item.hint.c_str());
    }

    if (zoomed) {
        lv_label_set_text(footer_lbl_, "▲▼换字  ●回整词");
    } else if (IsAiReady()) {
        lv_label_set_text(footer_lbl_, "▲▼换字 ●写好 长按问");
    } else {
        lv_label_set_text(footer_lbl_, "▲▼换字 ●写好 长按AI");
    }
}

void StudyCardPassport::RenderPhraseLocked(const StudyItem& item) {
    auto chars = study_logic::SplitUtf8(item.text);

    if (!item.sub.empty()) {
        lv_obj_t* py_lbl = lv_label_create(content_area_);
        lv_obj_set_style_text_font(py_lbl, fonts_.ui16(), 0);
        lv_obj_set_style_text_color(py_lbl, lv_color_hex(0x1D4ED8), 0);
        lv_obj_set_width(py_lbl, kScreenW - 24);
        lv_obj_set_style_text_align(py_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(py_lbl, 12, 6);
        lv_label_set_text(py_lbl, item.sub.c_str());
    }

    int32_t hint_y = 192;
    if (chars.size() == 2) {
        constexpr int32_t box_sz = 96;
        constexpr int32_t gap = 14;
        int32_t start_x = (kScreenW - (box_sz * 2 + gap)) / 2;
        int32_t box_y = 46;
        for (size_t i = 0; i < 2; ++i) {
            lv_obj_t* box = CreateTianzigeBox(content_area_, box_sz, grid_h_pts_[i], grid_v_pts_[i]);
            lv_obj_set_pos(box, start_x + static_cast<int32_t>(i) * (box_sz + gap), box_y);
            lv_obj_t* ch_lbl = lv_label_create(box);
            lv_obj_set_style_text_font(ch_lbl, fonts_.kai44(), 0);
            lv_obj_set_style_text_color(ch_lbl, lv_color_hex(0x1E293B), 0);
            lv_label_set_text(ch_lbl, chars[i].c_str());
            lv_obj_center(ch_lbl);
        }
        hint_y = box_y + box_sz + 18;
    } else if (chars.size() <= 4) {
        constexpr int32_t box_sz = 80;
        constexpr int32_t gap = 10;
        int32_t start_x = (kScreenW - (box_sz * 2 + gap)) / 2;
        int32_t start_y = 30;
        for (size_t i = 0; i < chars.size(); ++i) {
            int32_t col = static_cast<int32_t>(i % 2);
            int32_t row = static_cast<int32_t>(i / 2);
            lv_obj_t* box = CreateTianzigeBox(content_area_, box_sz, grid_h_pts_[i], grid_v_pts_[i]);
            lv_obj_set_pos(box, start_x + col * (box_sz + gap), start_y + row * (box_sz + gap));
            lv_obj_t* ch_lbl = lv_label_create(box);
            lv_obj_set_style_text_font(ch_lbl, fonts_.kai44(), 0);
            lv_obj_set_style_text_color(ch_lbl, lv_color_hex(0x1E293B), 0);
            lv_label_set_text(ch_lbl, chars[i].c_str());
            lv_obj_center(ch_lbl);
        }
        hint_y = start_y + box_sz * 2 + gap + 8;
    } else if (chars.size() <= kMaxGridBoxes) {
        constexpr int32_t box_sz = 66;
        constexpr int32_t gap = 8;
        int32_t start_x = (kScreenW - (box_sz * 3 + gap * 2)) / 2;
        int32_t start_y = 38;
        for (size_t i = 0; i < chars.size(); ++i) {
            int32_t col = static_cast<int32_t>(i % 3);
            int32_t row = static_cast<int32_t>(i / 3);
            lv_obj_t* box = CreateTianzigeBox(content_area_, box_sz, grid_h_pts_[i], grid_v_pts_[i]);
            lv_obj_set_pos(box, start_x + col * (box_sz + gap), start_y + row * (box_sz + gap));
            lv_obj_t* ch_lbl = lv_label_create(box);
            lv_obj_set_style_text_font(ch_lbl, fonts_.kai44(), 0);
            lv_obj_set_style_text_color(ch_lbl, lv_color_hex(0x1E293B), 0);
            lv_label_set_text(ch_lbl, chars[i].c_str());
            lv_obj_center(ch_lbl);
        }
        hint_y = start_y + box_sz * 2 + gap + 12;
    } else {
        lv_obj_t* lbl = lv_label_create(content_area_);
        lv_obj_set_style_text_font(lbl, chars.size() <= 12 ? fonts_.kai44() : fonts_.ui16(), 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x1E293B), 0);
        lv_obj_set_width(lbl, kScreenW - 24);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(lbl, 12, 44);
        lv_label_set_text(lbl, item.text.c_str());
        hint_y = 186;
    }

    if (!item.hint.empty()) {
        lv_obj_t* hint_lbl = lv_label_create(content_area_);
        lv_obj_set_style_text_font(hint_lbl, fonts_.ui16(), 0);
        lv_obj_set_style_text_color(hint_lbl, lv_color_hex(0x334155), 0);
        lv_obj_set_width(hint_lbl, kScreenW - 24);
        lv_obj_set_style_text_align(hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(hint_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(hint_lbl, 3, 0);
        lv_obj_set_pos(hint_lbl, 12, hint_y);
        lv_label_set_text(hint_lbl, item.hint.c_str());
    }

    lv_label_set_text(footer_lbl_, "长按▲放单字 ●写好");
}

void StudyCardPassport::RenderEnglishLocked(const StudyItem& item) {
    const int32_t ys[4] = {56, 71, 91, 105};
    const uint32_t colors[4] = {0x93C5FD, 0xCBD5E1, 0xF87171, 0x93C5FD};
    const bool dashed[4] = {false, true, false, false};

    for (int i = 0; i < 4; ++i) {
        staff_pts_[i][0] = {14, ys[i]};
        staff_pts_[i][1] = {kScreenW - 14, ys[i]};
        lv_obj_t* ln = lv_line_create(content_area_);
        lv_line_set_points(ln, staff_pts_[i], 2);
        lv_obj_set_style_line_color(ln, lv_color_hex(colors[i]), 0);
        lv_obj_set_style_line_width(ln, (i == 2) ? 2 : 1, 0);
        if (dashed[i]) {
            lv_obj_set_style_line_dash_width(ln, 5, 0);
            lv_obj_set_style_line_dash_gap(ln, 4, 0);
        }
    }

    bool use_large = (item.text.size() <= 11);
    lv_obj_t* word_lbl = lv_label_create(content_area_);
    lv_obj_set_style_text_font(word_lbl, use_large ? fonts_.latin36() : fonts_.ui16(), 0);
    lv_obj_set_style_text_color(word_lbl, lv_color_hex(0x0F172A), 0);
    lv_obj_set_width(word_lbl, kScreenW - 20);
    lv_obj_set_style_text_align(word_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(word_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(word_lbl, 10, use_large ? 56 : 70);
    lv_label_set_text(word_lbl, item.text.c_str());

    if (!item.sub.empty()) {
        lv_obj_t* ipa_lbl = lv_label_create(content_area_);
        lv_obj_set_style_text_font(ipa_lbl, fonts_.ui16(), 0);
        lv_obj_set_style_text_color(ipa_lbl, lv_color_hex(0x1D4ED8), 0);
        lv_obj_set_width(ipa_lbl, kScreenW - 24);
        lv_obj_set_style_text_align(ipa_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(ipa_lbl, 12, 124);
        lv_label_set_text(ipa_lbl, item.sub.c_str());
    }

    if (!item.hint.empty()) {
        lv_obj_t* hint_lbl = lv_label_create(content_area_);
        lv_obj_set_style_text_font(hint_lbl, fonts_.ui16(), 0);
        lv_obj_set_style_text_color(hint_lbl, lv_color_hex(0x334155), 0);
        lv_obj_set_width(hint_lbl, kScreenW - 24);
        lv_obj_set_style_text_align(hint_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(hint_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_line_space(hint_lbl, 5, 0);
        lv_obj_set_pos(hint_lbl, 12, 156);
        lv_label_set_text(hint_lbl, item.hint.c_str());
    }

    if (IsAiReady()) {
        lv_label_set_text(footer_lbl_, "▲▼换字 ●写好 长按问");
    } else {
        lv_label_set_text(footer_lbl_, "▲▼换字 ●写好 长按AI");
    }
}

void StudyCardPassport::RenderWordbookLocked() {
    const auto& items = sm_.items();
    if (items.empty()) {
        lv_obj_t* empty_lbl = lv_label_create(content_area_);
        lv_obj_set_style_text_font(empty_lbl, fonts_.ui16(), 0);
        lv_obj_set_style_text_color(empty_lbl, lv_color_hex(0x94A3B8), 0);
        lv_label_set_text(empty_lbl, "还没有生字记录");
        lv_obj_center(empty_lbl);
        lv_label_set_text(footer_lbl_, "长按●返回");
        return;
    }

    constexpr int kRows = 5;
    constexpr int32_t kRowH = 44;
    const int n = static_cast<int>(items.size());
    int cur = sm_.cursor();
    if (cur < 0 || cur >= n) cur = 0;

    int win_start = cur - kRows / 2;
    if (win_start > n - kRows) win_start = n - kRows;
    if (win_start < 0) win_start = 0;

    for (int r = 0; r < kRows && win_start + r < n; ++r) {
        int idx = win_start + r;
        const StudyItem& it = items[static_cast<size_t>(idx)];
        bool selected = (idx == cur);

        lv_obj_t* row = lv_obj_create(content_area_);
        lv_obj_set_size(row, kScreenW - 24, kRowH - 4);
        lv_obj_set_pos(row, 12, 12 + r * kRowH);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(selected ? 0xF59E0B : 0xE5DEC9), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(selected ? 0xFEF3C7 : 0xFFFDF8), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* text_lbl = lv_label_create(row);
        lv_obj_set_style_text_font(text_lbl, fonts_.ui16(), 0);
        lv_obj_set_style_text_color(text_lbl, lv_color_hex(0x0F172A), 0);
        lv_obj_set_pos(text_lbl, 10, 10);
        lv_obj_set_width(text_lbl, 118);
        lv_label_set_long_mode(text_lbl, LV_LABEL_LONG_CLIP);
        lv_label_set_text(text_lbl, it.text.c_str());

        char meta[32];
        snprintf(meta, sizeof(meta), "%s ×%u",
                 it.mode == "english" ? "EN" : (it.mode == "phrase" ? "词" : "字"),
                 (unsigned)it.ask_count);
        lv_obj_t* meta_lbl = lv_label_create(row);
        lv_obj_set_style_text_font(meta_lbl, fonts_.ui16(), 0);
        lv_obj_set_style_text_color(meta_lbl, lv_color_hex(0x64748B), 0);
        lv_obj_set_width(meta_lbl, 72);
        lv_obj_set_style_text_align(meta_lbl, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(meta_lbl, kScreenW - 24 - 82, 10);
        lv_label_set_text(meta_lbl, meta);
    }

    lv_label_set_text(footer_lbl_, "▲▼选字 ●查看 长按返");
}

void StudyCardPassport::RenderCardLocked() {
    StudyItem eff = sm_.EffectiveDisplayItem();
    if (eff.text.empty()) {
        HideLocked();
        return;
    }
    if (eff.mode == "english") {
        RenderEnglishLocked(eff);
    } else if (eff.mode == "phrase") {
        RenderPhraseLocked(eff);
    } else {
        RenderHanziLocked(eff, sm_.IsPhraseZoomed());
    }
}

void StudyCardPassport::RenderCurrentViewLocked(bool flash_star) {
    if (!overlay_ || !content_area_) {
        return;
    }
    if (sm_.view() == StudyView::kPetHome) {
        HideLocked();
        return;
    }

    ESP_LOGI(TAG, "RenderCurrentViewLocked view=%d free_heap=%u largest=%u",
             (int)sm_.view(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));

    lv_obj_clean(content_area_);
    RenderTopBarLocked(flash_star);
    if (sm_.view() == StudyView::kWordbook) {
        RenderWordbookLocked();
    } else {
        RenderCardLocked();
    }

    lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay_);
    ResetAutoHideTimer();
}

void StudyCardPassport::HideLocked() {
    if (overlay_) {
        lv_obj_add_flag(overlay_, LV_OBJ_FLAG_HIDDEN);
    }
    if (auto_hide_timer_) {
        esp_timer_stop(auto_hide_timer_);
    }
}

void StudyCardPassport::ResetAutoHideTimer() {
    if (!auto_hide_timer_) {
        return;
    }
    esp_timer_stop(auto_hide_timer_);
    // Only auto-return to AI Pet Home after 3 minutes when Xiaozhi AI is bound & ready.
    // While offline/unbound, keep the StudyCard visible so it never reverts to a pairing screen.
    if (IsAiReady()) {
        esp_timer_start_once(auto_hide_timer_, kAutoHideUs);
    }
}

void StudyCardPassport::PersistNvs() {
    StudyBookNvs::Save(sm_.items(), sm_.star_count());
}

void StudyCardPassport::Show(const std::string& mode, const std::string& text,
                             const std::string& sub, const std::string& hint) {
    if (!initialized_ || text.empty()) {
        return;
    }
    if (!sm_.PushAndShow(mode, text, sub, hint)) {
        return;
    }
    PersistNvs();

    if (lvgl_port_lock(0)) {
        RenderCurrentViewLocked();
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "Displayed study card [%s]: %s (%s)",
             sm_.CurrentItem()->mode.c_str(), text.c_str(), sub.c_str());
}

void StudyCardPassport::Hide() {
    if (!initialized_) {
        return;
    }
    sm_.HideToPetHome();
    if (lvgl_port_lock(0)) {
        HideLocked();
        lvgl_port_unlock();
    }
}

uint32_t StudyCardPassport::RewardStar(const std::string& reason) {
    uint32_t total = sm_.AddStar(1);
    PersistNvs();
    ESP_LOGI(TAG, "Star rewarded (+1 -> %lu): %s", (unsigned long)total, reason.c_str());
    if (initialized_ && sm_.view() != StudyView::kPetHome && lvgl_port_lock(0)) {
        RenderTopBarLocked(/*flash_star=*/true);
        lvgl_port_unlock();
    }
    return total;
}

StudyAction StudyCardPassport::HandleKey(PassportKey key, PassportKeyEvent ev) {
    if (!initialized_) {
        return StudyAction::kNone;
    }
    StudyAction act = sm_.HandleKey(key, ev, IsAiReady());
    if (act == StudyAction::kRenderView) {
        if (lvgl_port_lock(0)) {
            RenderCurrentViewLocked();
            lvgl_port_unlock();
        }
    } else if (act == StudyAction::kNextCardWithStar) {
        PersistNvs();
        if (lvgl_port_lock(0)) {
            RenderCurrentViewLocked(/*flash_star=*/true);
            lvgl_port_unlock();
        }
    } else if (act == StudyAction::kCloseToPetHome || act == StudyAction::kShowNetworkSetup) {
        if (lvgl_port_lock(0)) {
            HideLocked();
            lvgl_port_unlock();
        }
    } else if (act == StudyAction::kCloseAndRewardStar) {
        PersistNvs();
        if (lvgl_port_lock(0)) {
            HideLocked();
            lvgl_port_unlock();
        }
    }
    return act;
}

void StudyCardPassport::OnAutoHideTimer(void* arg) {
    auto* self = static_cast<StudyCardPassport*>(arg);
    if (self && self->IsOverlayVisible() && self->IsAiReady()) {
        ESP_LOGI(TAG, "Auto-hiding study overlay after 3-minute timeout");
        self->Hide();
    }
}
