#include "passport_display_rounding.h"
#include "passport_key_decoder.h"
#include "study_logic.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

void TestDisplayRounding() {
    int32_t x1 = 0, x2 = 0;
    assert(passport_display_rounded_row_span(160, 240, 320, 30, &x1, &x2));
    assert(x1 == 0 && x2 == 239);

    assert(passport_display_rounded_row_span(0, 240, 320, 30, &x1, &x2));
    assert(x1 == 30 && x2 == 209);
    assert(passport_display_pixel_outside_rounded_rect(0, 0, 240, 320, 30));
    assert(!passport_display_pixel_outside_rounded_rect(120, 0, 240, 320, 30));

    int32_t bx1 = 0, bx2 = 0;
    assert(passport_display_rounded_row_span(319, 240, 320, 30, &bx1, &bx2));
    assert(bx1 == x1 && bx2 == x2);
    printf("[PASS] TestDisplayRounding\n");
}

void TestKeyDecoder() {
    assert(PassportKeyDecoder::WindowOf(-1) == -1);
    assert(PassportKeyDecoder::WindowOf(0) == 0);
    assert(PassportKeyDecoder::WindowOf(149) == 0);
    assert(PassportKeyDecoder::WindowOf(150) == 1);
    assert(PassportKeyDecoder::WindowOf(300) == 1);
    assert(PassportKeyDecoder::WindowOf(447) == 2);
    assert(PassportKeyDecoder::WindowOf(595) == 2);
    assert(PassportKeyDecoder::WindowOf(1900) == -1);
    assert(PassportKeyDecoder::WindowOf(3300) == -1);

    PassportKeyDecoder dec;
    assert(dec.Feed(2, 0) == -1);
    assert(dec.Feed(2, 30000) == -1);
    assert(dec.Feed(2, 40000) == 2);

    assert(dec.Feed(1, 50000) == 2);
    assert(dec.Feed(1, 100000) == 2);
    assert(dec.Feed(2, 110000) == 2);
    assert(dec.glitches == 1);

    assert(dec.Feed(-1, 200000) == 2);
    assert(dec.Feed(-1, 319000) == 2);
    assert(dec.Feed(-1, 320000) == -1);
    printf("[PASS] TestKeyDecoder\n");
}

void TestStateMachineOfflineAndOnline() {
    StudyStateMachine sm;
    // Boot opens StudyCard directly with 12 curated starter cards!
    sm.OpenStudyCard();
    assert(sm.view() == StudyView::kStudyCard);
    assert(sm.items().size() == 12);
    assert(sm.cursor() == 0);

    // Offline/unbound: OK click awards star and advances to next card (never trapped!)
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kClick, /*ai_ready=*/false) ==
           StudyAction::kNextCardWithStar);
    assert(sm.view() == StudyView::kStudyCard);
    assert(sm.star_count() == 1);
    assert(sm.cursor() == 1);
    assert(sm.CurrentItem()->text == "蝴蝶");

    // Offline/unbound: Long press UP/DOWN on phrase zooms into single character
    assert(sm.HandleKey(PassportKey::kDown, PassportKeyEvent::kLongPress, false) ==
           StudyAction::kRenderView);
    assert(sm.IsPhraseZoomed());
    assert(sm.EffectiveDisplayItem().mode == "hanzi");
    assert(sm.EffectiveDisplayItem().text == "蝴");
    assert(sm.HandleKey(PassportKey::kDown, PassportKeyEvent::kClick, false) ==
           StudyAction::kRenderView);
    assert(sm.EffectiveDisplayItem().text == "蝶");
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kClick, false) ==
           StudyAction::kRenderView);
    assert(!sm.IsPhraseZoomed());

    // Offline/unbound: Long press OK reveals Wi-Fi/pairing screen; short press OK returns!
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kLongPress, false) ==
           StudyAction::kShowNetworkSetup);
    assert(sm.view() == StudyView::kPetHome);
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kClick, false) ==
           StudyAction::kRenderView);
    assert(sm.view() == StudyView::kStudyCard);

    // Offline/unbound: Double click OK opens wordbook; long press OK returns to StudyCard!
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kDoubleClick, false) ==
           StudyAction::kRenderView);
    assert(sm.view() == StudyView::kWordbook);
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kLongPress, false) ==
           StudyAction::kRenderView);
    assert(sm.view() == StudyView::kStudyCard);

    // Online (ai_ready=true): OK click closes to PetHome & rewards star; PetHome OK toggles chat
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kClick, /*ai_ready=*/true) ==
           StudyAction::kCloseAndRewardStar);
    assert(sm.view() == StudyView::kPetHome);
    assert(sm.star_count() == 2);
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kClick, true) ==
           StudyAction::kToggleVoiceChat);
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kLongPress, true) ==
           StudyAction::kEnterWifiConfig);

    // Ring buffer capacity cap (32 items)
    for (int i = 0; i < 40; ++i) {
        sm.PushAndShow("english", "word_" + std::to_string(i), "", "");
    }
    assert(sm.items().size() == StudyStateMachine::kMaxItems);
    printf("[PASS] TestStateMachineOfflineAndOnline\n");
}

void TestSfp2PackAndDecoders(const char* pack_path) {
    std::ifstream ifs(pack_path, std::ios::binary);
    assert(ifs.good());
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(ifs)), {});
    assert(data.size() > 64 && data.size() <= 0x420000);

    struct __attribute__((packed)) PackHdr {
        char magic[4];
        uint16_t version;
        uint16_t font_count;
        uint32_t total_size;
        uint32_t crc32;
        uint32_t reserved[4];
    };
    struct __attribute__((packed)) FontDir {
        char name[12];
        uint16_t lh;
        uint16_t bl;
        uint8_t bpp;
        uint8_t enc;
        uint16_t rsv;
        uint32_t count;
        uint32_t idx_off;
        uint32_t data_base;
    };
    struct __attribute__((packed)) GlyphEntry {
        uint32_t cp;
        uint32_t data_off;
        uint8_t w;
        uint8_t h;
        int8_t ox;
        int8_t oy;
        uint8_t adv;
        uint8_t rsv;
        uint16_t data_sz;
    };

    PackHdr hdr = {};
    std::memcpy(&hdr, data.data(), sizeof(hdr));
    assert(std::memcmp(hdr.magic, "SFP2", 4) == 0);
    assert(hdr.version == 2);
    assert(hdr.font_count == 4);
    assert(hdr.total_size == data.size());
    uint32_t calc = study_logic::Crc32(data.data() + 16, data.size() - 16);
    assert(calc == hdr.crc32);

    // Verify all 12 starter cards' UTF-8 characters decode cleanly from the pack!
    StudyStateMachine sm;
    sm.EnsureStarterItems();
    auto find_cp = [&](const FontDir& dir, uint32_t cp, GlyphEntry* out) -> bool {
        int lo = 0, hi = static_cast<int>(dir.count) - 1;
        while (lo <= hi) {
            int mid = lo + ((hi - lo) >> 1);
            GlyphEntry e = {};
            std::memcpy(&e, data.data() + dir.idx_off + mid * sizeof(GlyphEntry), sizeof(e));
            if (e.cp == cp) { *out = e; return true; }
            if (e.cp < cp) lo = mid + 1;
            else hi = mid - 1;
        }
        return false;
    };

    FontDir dirs[4] = {};
    for (int f = 0; f < 4; ++f) {
        std::memcpy(&dirs[f], data.data() + sizeof(PackHdr) + f * sizeof(FontDir), sizeof(FontDir));
    }

    auto utf8_to_cps = [](const std::string& s) {
        std::vector<uint32_t> cps;
        size_t i = 0;
        while (i < s.size()) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            uint32_t cp = 0;
            size_t len = 1;
            if ((c & 0x80) == 0) { cp = c; len = 1; }
            else if ((c & 0xE0) == 0xC0) { cp = ((c & 0x1F) << 6) | (s[i+1] & 0x3F); len = 2; }
            else if ((c & 0xF0) == 0xE0) { cp = ((c & 0x0F) << 12) | ((s[i+1] & 0x3F) << 6) | (s[i+2] & 0x3F); len = 3; }
            else { cp = ((c & 0x07) << 18) | ((s[i+1] & 0x3F) << 12) | ((s[i+2] & 0x3F) << 6) | (s[i+3] & 0x3F); len = 4; }
            if (cp != '\n' && cp != '\r') cps.push_back(cp);
            i += len;
        }
        return cps;
    };

    // Verify every single character across all 12 starter cards & UI footers exists in ui_16 (dirs[2])
    const std::string ui_strings = "▲▼换字 ●写好 长按AI 长按问 长按▲放单字 ●回整词 选字 查看 返 生字本";
    for (uint32_t cp : utf8_to_cps(ui_strings)) {
        GlyphEntry e = {};
        assert(find_cp(dirs[2], cp, &e));
    }
    for (const auto& it : sm.items()) {
        for (uint32_t cp : utf8_to_cps(it.sub + it.hint)) {
            GlyphEntry e = {};
            assert(find_cp(dirs[2], cp, &e));
        }
        for (uint32_t cp : utf8_to_cps(it.text)) {
            GlyphEntry e = {};
            int fidx = (it.mode == "english") ? 3 : ((it.mode == "phrase") ? 1 : 0);
            assert(find_cp(dirs[fidx], cp, &e));
            if (e.w > 0 && e.h > 0) {
                std::vector<uint8_t> out(static_cast<size_t>(e.w) * e.h, 0xCC);
                const uint8_t* src = data.data() + dirs[fidx].data_base + e.data_off;
                size_t n = (dirs[fidx].enc == 1)
                               ? study_logic::DecodeRle2ToA8(src, e.data_sz, e.w, e.h, out.data(), e.w)
                               : study_logic::DecodeRaw4ToA8(src, e.data_sz, e.w, e.h, out.data(), e.w);
                assert(n == static_cast<size_t>(e.w) * e.h);
            }
        }
    }
    printf("[PASS] TestSfp2PackAndDecoders (12 starter cards + UI footers verified, CRC32=0x%08X)\n", calc);
}

}  // namespace

int main(int argc, char** argv) {
    TestDisplayRounding();
    TestKeyDecoder();
    TestStateMachineOfflineAndOnline();
    if (argc > 1) {
        TestSfp2PackAndDecoders(argv[1]);
    }
    printf("ALL HOST TESTS PASSED!\n");
    return 0;
}
