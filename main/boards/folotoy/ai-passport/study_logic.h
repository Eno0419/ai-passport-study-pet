#ifndef STUDY_LOGIC_H_
#define STUDY_LOGIC_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct StudyItem {
    std::string mode;  // "hanzi", "phrase", "english"
    std::string text;  // Main character / phrase / English word
    std::string sub;   // Pinyin (with tones) or IPA phonetic
    std::string hint;  // Radical / examples / Chinese meaning
    uint16_t ask_count = 1;
};

enum class StudyView : uint8_t {
    kPetHome = 0,
    kStudyCard = 1,
    kWordbook = 2,
};

enum class PassportKey : uint8_t {
    kUp = 0,
    kDown = 1,
    kOk = 2,
};

enum class PassportKeyEvent : uint8_t {
    kClick = 0,
    kDoubleClick = 1,
    kLongPress = 2,
};

enum class StudyAction : uint8_t {
    kNone = 0,
    kToggleVoiceChat,        // OK click on PetHome when AI ready -> toggle voice chat
    kStartVoiceFollowUp,     // OK long-press on StudyCard when AI ready -> ask follow-up
    kShowNetworkSetup,       // OK long-press on StudyCard when offline -> show Wi-Fi/binding screen
    kEnterWifiConfig,        // OK long-press on PetHome -> enter Wi-Fi config AP
    kVolumeUp,               // UP long-press (PetHome or non-phrase card)
    kVolumeDown,             // DOWN long-press (PetHome or non-phrase card)
    kRenderView,             // Re-render current view (Card or Wordbook)
    kNextCardWithStar,       // Offline/unbound OK click on card -> +1 star & advance to next card
    kCloseToPetHome,         // Close overlay back to PetHome / setup screen
    kCloseAndRewardStar,     // Online OK click on card -> +1 star & close to PetHome
};

namespace study_logic {

std::vector<std::string> SplitUtf8(const std::string& s);
std::string DetectMode(const std::string& req_mode, const std::string& text);

// IEEE CRC32 (matches Python zlib.crc32)
uint32_t Crc32(const uint8_t* data, size_t len);

// Decode 2bpp RLE stream (SFP2 encoding=1) into A8 opacity rows with target stride.
size_t DecodeRle2ToA8(const uint8_t* src, size_t src_len,
                      uint16_t box_w, uint16_t box_h,
                      uint8_t* dst, uint32_t dst_stride);

// Decode row-aligned 4bpp packed stream (SFP2 encoding=0, bpp=4) into A8 opacity rows.
size_t DecodeRaw4ToA8(const uint8_t* src, size_t src_len,
                      uint16_t box_w, uint16_t box_h,
                      uint8_t* dst, uint32_t dst_stride);

}  // namespace study_logic

// Pure, host-testable state machine for the 3-key AI Passport Study Pet UI.
class StudyStateMachine {
public:
    static constexpr size_t kMaxItems = 32;

    void LoadSaved(std::vector<StudyItem> items, uint32_t star_count);
    void EnsureStarterItems();

    // Open StudyCard immediately (used on boot so users can study offline right away).
    void OpenStudyCard();

    // Add or update an item and show it on StudyCard. Returns true if history changed.
    bool PushAndShow(const std::string& req_mode, const std::string& text,
                     const std::string& sub, const std::string& hint);

    // Hide overlay (e.g., MCP self.study.close or auto-hide timeout).
    void HideToPetHome();

    // Reward star (from MCP self.pet.reward_star or finishing a card).
    uint32_t AddStar(uint32_t delta = 1);

    // Handle a 3-key event. `ai_ready` is true when online & bound to Xiaozhi AI
    // (idle/listening/speaking), and false during Wi-Fi setup or pairing-code screen.
    StudyAction HandleKey(PassportKey key, PassportKeyEvent ev, bool ai_ready = true);

    StudyView view() const { return view_; }
    const std::vector<StudyItem>& items() const { return items_; }
    int cursor() const { return cursor_; }
    int zoom_char_index() const { return zoom_char_index_; }
    uint32_t star_count() const { return star_count_; }

    const StudyItem* CurrentItem() const;
    // If zoomed into a single character of a phrase, returns a synthetic single-hanzi item.
    StudyItem EffectiveDisplayItem() const;
    bool IsPhraseZoomed() const { return zoom_char_index_ >= 0; }

private:
    std::vector<StudyItem> items_;
    int cursor_ = -1;
    int zoom_char_index_ = -1;  // -1 = normal view; >=0 = zoomed char index inside phrase
    uint32_t star_count_ = 0;
    StudyView view_ = StudyView::kPetHome;
};

#endif  // STUDY_LOGIC_H_
