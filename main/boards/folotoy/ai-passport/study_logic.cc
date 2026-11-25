#include "study_logic.h"

#include <algorithm>
#include <cstring>

namespace study_logic {

static constexpr uint8_t kOpa2Table[4] = {0, 85, 170, 255};
static constexpr uint8_t kOpa4Table[16] = {
    0, 17, 34, 51, 68, 85, 102, 119,
    136, 153, 170, 187, 204, 221, 238, 255,
};

std::vector<std::string> SplitUtf8(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((c & 0x80) == 0x00) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (i + len > s.size()) len = s.size() - i;
        std::string ch = s.substr(i, len);
        if (ch != " " && ch != "\n" && ch != "\r" && ch != "\t") {
            out.push_back(std::move(ch));
        }
        i += len;
    }
    return out;
}

std::string DetectMode(const std::string& req_mode, const std::string& text) {
    if (req_mode == "english" || req_mode == "en") {
        return "english";
    }
    bool all_ascii = !text.empty();
    for (unsigned char c : text) {
        if (c >= 0x80) {
            all_ascii = false;
            break;
        }
    }
    if (all_ascii) {
        return "english";
    }
    auto chars = SplitUtf8(text);
    if (chars.size() <= 1) {
        return "hanzi";
    }
    return "phrase";
}

uint32_t Crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return ~crc;
}

size_t DecodeRle2ToA8(const uint8_t* src, size_t src_len,
                      uint16_t box_w, uint16_t box_h,
                      uint8_t* dst, uint32_t dst_stride) {
    if (!src || !dst || box_w == 0 || box_h == 0) {
        return 0;
    }
    const size_t total = static_cast<size_t>(box_w) * static_cast<size_t>(box_h);
    size_t produced = 0;
    uint16_t x = 0;
    uint16_t y = 0;
    uint8_t* row = dst;

    auto emit_pixel = [&](uint8_t opa) {
        row[x++] = opa;
        ++produced;
        if (x == box_w) {
            x = 0;
            ++y;
            row += dst_stride;
        }
    };

    for (size_t i = 0; i < src_len && produced < total; ++i) {
        const uint8_t b = src[i];
        if ((b & 0xC0u) == 0xC0u) {
            size_t cnt = (static_cast<size_t>(b & 0x3Fu) + 1u) * 4u;
            size_t rem = total - produced;
            if (cnt > rem) cnt = rem;
            while (cnt > 0) {
                const size_t row_rem = box_w - x;
                const size_t step = cnt < row_rem ? cnt : row_rem;
                std::memset(row + x, 0, step);
                x += static_cast<uint16_t>(step);
                produced += step;
                cnt -= step;
                if (x == box_w) {
                    x = 0;
                    ++y;
                    row += dst_stride;
                }
            }
        } else if ((b & 0x80u) == 0x80u) {
            const uint8_t p[3] = {
                kOpa2Table[(b >> 4) & 0x03u],
                kOpa2Table[(b >> 2) & 0x03u],
                kOpa2Table[b & 0x03u],
            };
            for (int k = 0; k < 3 && produced < total; ++k) {
                emit_pixel(p[k]);
            }
        } else {
            const uint8_t opa = kOpa2Table[(b >> 5) & 0x03u];
            size_t cnt = static_cast<size_t>(b & 0x1Fu) + 1u;
            size_t rem = total - produced;
            if (cnt > rem) cnt = rem;
            while (cnt > 0) {
                const size_t row_rem = box_w - x;
                const size_t step = cnt < row_rem ? cnt : row_rem;
                std::memset(row + x, opa, step);
                x += static_cast<uint16_t>(step);
                produced += step;
                cnt -= step;
                if (x == box_w) {
                    x = 0;
                    ++y;
                    row += dst_stride;
                }
            }
        }
    }

    // Zero-fill any tail if stream ended early
    while (produced < total) {
        emit_pixel(0);
    }
    return produced;
}

size_t DecodeRaw4ToA8(const uint8_t* src, size_t src_len,
                      uint16_t box_w, uint16_t box_h,
                      uint8_t* dst, uint32_t dst_stride) {
    if (!src || !dst || box_w == 0 || box_h == 0) {
        return 0;
    }
    const uint8_t* ptr = src;
    const uint8_t* end = src + src_len;
    uint8_t* row = dst;
    size_t produced = 0;
    for (uint16_t y = 0; y < box_h; ++y) {
        uint16_t x = 0;
        for (; x + 1 < box_w; x += 2) {
            uint8_t b = (ptr < end) ? *ptr++ : 0;
            row[x] = kOpa4Table[b >> 4];
            row[x + 1] = kOpa4Table[b & 0x0Fu];
            produced += 2;
        }
        if (x < box_w) {
            uint8_t b = (ptr < end) ? *ptr++ : 0;
            row[x] = kOpa4Table[b >> 4];
            ++produced;
        }
        row += dst_stride;
    }
    return produced;
}

}  // namespace study_logic

void StudyStateMachine::LoadSaved(std::vector<StudyItem> items, uint32_t star_count) {
    if (items.size() > kMaxItems) {
        items.erase(items.begin(), items.begin() + static_cast<ptrdiff_t>(items.size() - kMaxItems));
    }
    items_ = std::move(items);
    cursor_ = items_.empty() ? -1 : static_cast<int>(items_.size()) - 1;
    zoom_char_index_ = -1;
    star_count_ = star_count;
}

void StudyStateMachine::EnsureStarterItems() {
    if (!items_.empty()) {
        return;
    }
    items_.push_back({"hanzi", "勇", "yǒng", "部首：力 · 9画\n组词：勇敢 · 勇气", 1});
    items_.push_back({"phrase", "蝴蝶", "hú dié", "美丽的昆虫，翅膀上有彩色花纹", 1});
    items_.push_back({"english", "butterfly", "/ˈbʌtəflaɪ/", "蝴蝶 · Cute butterfly!", 1});
    cursor_ = 0;
    zoom_char_index_ = -1;
}

bool StudyStateMachine::PushAndShow(const std::string& req_mode, const std::string& text,
                                    const std::string& sub, const std::string& hint) {
    if (text.empty()) {
        return false;
    }
    StudyItem item;
    item.mode = study_logic::DetectMode(req_mode, text);
    item.text = text;
    item.sub = sub;
    item.hint = hint;
    item.ask_count = 1;

    // Deduplicate by text: move to back and bump ask_count
    auto it = std::find_if(items_.begin(), items_.end(),
                           [&](const StudyItem& e) { return e.text == item.text; });
    if (it != items_.end()) {
        item.ask_count = static_cast<uint16_t>(std::min<uint32_t>(999, it->ask_count + 1));
        items_.erase(it);
    } else if (items_.size() >= kMaxItems) {
        items_.erase(items_.begin());
    }
    items_.push_back(std::move(item));
    cursor_ = static_cast<int>(items_.size()) - 1;
    zoom_char_index_ = -1;
    view_ = StudyView::kStudyCard;
    return true;
}

void StudyStateMachine::HideToPetHome() {
    zoom_char_index_ = -1;
    view_ = StudyView::kPetHome;
}

uint32_t StudyStateMachine::AddStar(uint32_t delta) {
    star_count_ += delta;
    return star_count_;
}

const StudyItem* StudyStateMachine::CurrentItem() const {
    if (cursor_ < 0 || cursor_ >= static_cast<int>(items_.size())) {
        return nullptr;
    }
    return &items_[static_cast<size_t>(cursor_)];
}

StudyItem StudyStateMachine::EffectiveDisplayItem() const {
    const StudyItem* cur = CurrentItem();
    if (!cur) {
        return {};
    }
    if (zoom_char_index_ >= 0 && cur->mode == "phrase") {
        auto chars = study_logic::SplitUtf8(cur->text);
        if (zoom_char_index_ < static_cast<int>(chars.size())) {
            StudyItem zoomed;
            zoomed.mode = "hanzi";
            zoomed.text = chars[static_cast<size_t>(zoom_char_index_)];
            zoomed.sub = cur->sub;
            zoomed.hint = "词语：" + cur->text +
                          (cur->hint.empty() ? "" : ("\n" + cur->hint));
            zoomed.ask_count = cur->ask_count;
            return zoomed;
        }
    }
    return *cur;
}

StudyAction StudyStateMachine::HandleKey(PassportKey key, PassportKeyEvent ev) {
    if (view_ == StudyView::kPetHome) {
        if (key == PassportKey::kOk) {
            if (ev == PassportKeyEvent::kClick) {
                return StudyAction::kToggleVoiceChat;
            }
            if (ev == PassportKeyEvent::kDoubleClick) {
                EnsureStarterItems();
                if (cursor_ < 0) cursor_ = static_cast<int>(items_.size()) - 1;
                zoom_char_index_ = -1;
                view_ = StudyView::kWordbook;
                return StudyAction::kRenderView;
            }
            if (ev == PassportKeyEvent::kLongPress) {
                return StudyAction::kEnterWifiConfig;
            }
        } else if (key == PassportKey::kUp) {
            if (ev == PassportKeyEvent::kClick) {
                EnsureStarterItems();
                if (cursor_ < 0) cursor_ = static_cast<int>(items_.size()) - 1;
                zoom_char_index_ = -1;
                view_ = StudyView::kStudyCard;
                return StudyAction::kRenderView;
            }
            if (ev == PassportKeyEvent::kLongPress) {
                return StudyAction::kVolumeUp;
            }
        } else if (key == PassportKey::kDown) {
            if (ev == PassportKeyEvent::kClick) {
                EnsureStarterItems();
                if (cursor_ < 0) cursor_ = static_cast<int>(items_.size()) - 1;
                zoom_char_index_ = -1;
                view_ = StudyView::kWordbook;
                return StudyAction::kRenderView;
            }
            if (ev == PassportKeyEvent::kLongPress) {
                return StudyAction::kVolumeDown;
            }
        }
        return StudyAction::kNone;
    }

    if (view_ == StudyView::kStudyCard) {
        if (key == PassportKey::kOk) {
            if (ev == PassportKeyEvent::kClick) {
                if (zoom_char_index_ >= 0) {
                    // Exit single-char zoom back to full phrase card first
                    zoom_char_index_ = -1;
                    return StudyAction::kRenderView;
                }
                AddStar(1);
                view_ = StudyView::kPetHome;
                return StudyAction::kCloseAndRewardStar;
            }
            if (ev == PassportKeyEvent::kLongPress) {
                return StudyAction::kStartVoiceFollowUp;
            }
            if (ev == PassportKeyEvent::kDoubleClick) {
                zoom_char_index_ = -1;
                view_ = StudyView::kWordbook;
                return StudyAction::kRenderView;
            }
        } else if (key == PassportKey::kUp || key == PassportKey::kDown) {
            const int dir = (key == PassportKey::kUp) ? -1 : 1;
            const StudyItem* cur = CurrentItem();
            if (ev == PassportKeyEvent::kLongPress && cur && cur->mode == "phrase") {
                auto chars = study_logic::SplitUtf8(cur->text);
                if (chars.size() > 1) {
                    if (zoom_char_index_ < 0) {
                        zoom_char_index_ = (dir > 0) ? 0 : static_cast<int>(chars.size()) - 1;
                    } else {
                        zoom_char_index_ = (zoom_char_index_ + dir + static_cast<int>(chars.size())) %
                                           static_cast<int>(chars.size());
                    }
                    return StudyAction::kRenderView;
                }
            }
            if (ev == PassportKeyEvent::kClick && !items_.empty()) {
                if (zoom_char_index_ >= 0 && cur && cur->mode == "phrase") {
                    auto chars = study_logic::SplitUtf8(cur->text);
                    if (!chars.empty()) {
                        zoom_char_index_ = (zoom_char_index_ + dir + static_cast<int>(chars.size())) %
                                           static_cast<int>(chars.size());
                        return StudyAction::kRenderView;
                    }
                }
                const int n = static_cast<int>(items_.size());
                cursor_ = (cursor_ + dir + n) % n;
                zoom_char_index_ = -1;
                return StudyAction::kRenderView;
            }
        }
        return StudyAction::kNone;
    }

    // StudyView::kWordbook
    if (key == PassportKey::kUp && ev == PassportKeyEvent::kClick && !items_.empty()) {
        const int n = static_cast<int>(items_.size());
        cursor_ = (cursor_ - 1 + n) % n;
        return StudyAction::kRenderView;
    }
    if (key == PassportKey::kDown && ev == PassportKeyEvent::kClick && !items_.empty()) {
        const int n = static_cast<int>(items_.size());
        cursor_ = (cursor_ + 1) % n;
        return StudyAction::kRenderView;
    }
    if (key == PassportKey::kOk) {
        if (ev == PassportKeyEvent::kClick && !items_.empty()) {
            zoom_char_index_ = -1;
            view_ = StudyView::kStudyCard;
            return StudyAction::kRenderView;
        }
        if (ev == PassportKeyEvent::kLongPress || ev == PassportKeyEvent::kDoubleClick) {
            zoom_char_index_ = -1;
            view_ = StudyView::kPetHome;
            return StudyAction::kCloseToPetHome;
        }
    }
    return StudyAction::kNone;
}
