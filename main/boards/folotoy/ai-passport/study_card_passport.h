#ifndef STUDY_CARD_PASSPORT_H_
#define STUDY_CARD_PASSPORT_H_

#include "study_flash_font.h"
#include "study_logic.h"

#include <esp_timer.h>
#include <lvgl.h>
#include <functional>
#include <string>

// Portrait 240x320 LVGL StudyCard & Wordbook overlay designed for FoloToy AI Passport
// (R=30px rounded bezel, 3 physical keys, zero PSRAM, Flash SFP2 KaiTi fonts).
class StudyCardPassport {
public:
    static StudyCardPassport& GetInstance() {
        static StudyCardPassport instance;
        return instance;
    }

    // Call once after LVGL display is ready (outside LVGL lock).
    // Launches directly into the offline-ready StudyCard so the device is immediately
    // usable without waiting for Wi-Fi or cloud pairing.
    void Initialize(const lv_font_t* builtin_fallback_font,
                    std::function<int()> battery_soc_provider,
                    std::function<bool()> ai_ready_provider);

    // Thread-safe methods callable from MCP / app / button tasks.
    void Show(const std::string& mode, const std::string& text,
              const std::string& sub, const std::string& hint);
    void Hide();
    uint32_t RewardStar(const std::string& reason);

    // Dispatch a 3-key event (must be called on Application task, NOT inside ISR/timer).
    StudyAction HandleKey(PassportKey key, PassportKeyEvent ev);

    bool IsOverlayVisible() const { return sm_.view() != StudyView::kPetHome; }
    StudyView GetView() const { return sm_.view(); }
    uint32_t GetStarCount() const { return sm_.star_count(); }
    const lv_font_t* GetUiFont() const { return fonts_.ui16(); }

private:
    StudyCardPassport() = default;
    ~StudyCardPassport();

    StudyCardPassport(const StudyCardPassport&) = delete;
    StudyCardPassport& operator=(const StudyCardPassport&) = delete;

    bool IsAiReady() const { return ai_ready_provider_ && ai_ready_provider_(); }
    void CreateOverlayLocked();
    void RenderCurrentViewLocked(bool flash_star = false);
    void RenderTopBarLocked(bool flash_star = false);
    void RenderCardLocked();
    void RenderHanziLocked(const StudyItem& item, bool zoomed);
    void RenderPhraseLocked(const StudyItem& item);
    void RenderEnglishLocked(const StudyItem& item);
    void RenderWordbookLocked();
    void HideLocked();
    void ResetAutoHideTimer();
    void PersistNvs();

    lv_obj_t* CreateTianzigeBox(lv_obj_t* parent, int32_t size,
                                lv_point_precise_t* h_pts, lv_point_precise_t* v_pts);

    static void OnAutoHideTimer(void* arg);

    StudyFontPack fonts_;
    StudyStateMachine sm_;
    std::function<int()> battery_soc_provider_;
    std::function<bool()> ai_ready_provider_;

    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* top_star_lbl_ = nullptr;
    lv_obj_t* top_page_lbl_ = nullptr;
    lv_obj_t* top_batt_lbl_ = nullptr;
    lv_obj_t* content_area_ = nullptr;
    lv_obj_t* footer_lbl_ = nullptr;

    static constexpr size_t kMaxGridBoxes = 6;
    lv_point_precise_t grid_h_pts_[kMaxGridBoxes][2] = {};
    lv_point_precise_t grid_v_pts_[kMaxGridBoxes][2] = {};
    lv_point_precise_t staff_pts_[4][2] = {};

    esp_timer_handle_t auto_hide_timer_ = nullptr;
    bool initialized_ = false;
};

#endif  // STUDY_CARD_PASSPORT_H_
