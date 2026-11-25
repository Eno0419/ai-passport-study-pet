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
    // Middle row must span full width [0, 239]
    assert(passport_display_rounded_row_span(160, 240, 320, 30, &x1, &x2));
    assert(x1 == 0 && x2 == 239);

    // Top row y=0 has tightest inset (x1=30, x2=209)
    assert(passport_display_rounded_row_span(0, 240, 320, 30, &x1, &x2));
    assert(x1 == 30 && x2 == 209);
    assert(passport_display_pixel_outside_rounded_rect(0, 0, 240, 320, 30));
    assert(!passport_display_pixel_outside_rounded_rect(120, 0, 240, 320, 30));

    // Bottom symmetry: y=319 matches y=0
    int32_t bx1 = 0, bx2 = 0;
    assert(passport_display_rounded_row_span(319, 240, 320, 30, &bx1, &bx2));
    assert(bx1 == x1 && bx2 == x2);
    printf("[PASS] TestDisplayRounding\n");
}

void TestKeyDecoder() {
    assert(PassportKeyDecoder::WindowOf(-1) == -1);
    assert(PassportKeyDecoder::WindowOf(0) == 0);     // UP
    assert(PassportKeyDecoder::WindowOf(149) == 0);
    assert(PassportKeyDecoder::WindowOf(150) == 1);   // DOWN
    assert(PassportKeyDecoder::WindowOf(300) == 1);
    assert(PassportKeyDecoder::WindowOf(447) == 2);   // OK
    assert(PassportKeyDecoder::WindowOf(595) == 2);
    assert(PassportKeyDecoder::WindowOf(1900) == -1); // Open
    assert(PassportKeyDecoder::WindowOf(3300) == -1);

    PassportKeyDecoder dec;
    // Confirm press of OK after 40ms
    assert(dec.Feed(2, 0) == -1);
    assert(dec.Feed(2, 30000) == -1);
    assert(dec.Feed(2, 40000) == 2);

    // Transient contact glitch sweeping through DOWN for 60ms (<120ms hold) must be absorbed!
    assert(dec.Feed(1, 50000) == 2);
    assert(dec.Feed(1, 100000) == 2);
    assert(dec.Feed(2, 110000) == 2);
    assert(dec.glitches == 1);

    // Genuine release held for 120ms releases the lock
    assert(dec.Feed(-1, 200000) == 2);
    assert(dec.Feed(-1, 319000) == 2);
    assert(dec.Feed(-1, 320000) == -1);
    printf("[PASS] TestKeyDecoder\n");
}

void TestStateMachine() {
    StudyStateMachine sm;
    assert(sm.view() == StudyView::kPetHome);

    // PetHome OK click -> toggle voice chat
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kClick) == StudyAction::kToggleVoiceChat);
    // PetHome OK long press -> enter Wi-Fi config
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kLongPress) == StudyAction::kEnterWifiConfig);
    // PetHome UP click -> opens starter cards
    assert(sm.HandleKey(PassportKey::kUp, PassportKeyEvent::kClick) == StudyAction::kRenderView);
    assert(sm.view() == StudyView::kStudyCard);
    assert(sm.items().size() == 3);

    // Push new phrase item via MCP
    assert(sm.PushAndShow("phrase", "蝴蝶", "hú dié", "昆虫"));
    assert(sm.CurrentItem()->text == "蝴蝶");
    assert(sm.CurrentItem()->ask_count == 2);  // deduplicated with starter!

    // Long press UP on phrase -> zooms into individual character!
    assert(sm.HandleKey(PassportKey::kDown, PassportKeyEvent::kLongPress) == StudyAction::kRenderView);
    assert(sm.IsPhraseZoomed());
    assert(sm.EffectiveDisplayItem().mode == "hanzi");
    assert(sm.EffectiveDisplayItem().text == "蝴");
    // Short press DOWN while zoomed -> next character in phrase
    assert(sm.HandleKey(PassportKey::kDown, PassportKeyEvent::kClick) == StudyAction::kRenderView);
    assert(sm.EffectiveDisplayItem().text == "蝶");
    // Short press OK while zoomed -> returns to whole phrase (not closing card!)
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kClick) == StudyAction::kRenderView);
    assert(!sm.IsPhraseZoomed());
    assert(sm.view() == StudyView::kStudyCard);

    // Short press OK on StudyCard -> close & reward star
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kClick) == StudyAction::kCloseAndRewardStar);
    assert(sm.view() == StudyView::kPetHome);
    assert(sm.star_count() == 1);

    // Open Wordbook via DOWN click, navigate, open card, long-press back
    assert(sm.HandleKey(PassportKey::kDown, PassportKeyEvent::kClick) == StudyAction::kRenderView);
    assert(sm.view() == StudyView::kWordbook);
    assert(sm.HandleKey(PassportKey::kOk, PassportKeyEvent::kLongPress) == StudyAction::kCloseToPetHome);
    assert(sm.view() == StudyView::kPetHome);

    // Ring buffer capacity cap (32 items)
    for (int i = 0; i < 40; ++i) {
        sm.PushAndShow("english", "word_" + std::to_string(i), "", "");
    }
    assert(sm.items().size() == StudyStateMachine::kMaxItems);
    printf("[PASS] TestStateMachine\n");
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

    // Decode sample glyphs from every font using C++ decoders
    const uint32_t test_cps[4] = {0x52C7, 0x8776, 0x0101, 'b'};  // 勇, 蝶, ā, b
    for (uint16_t f = 0; f < hdr.font_count; ++f) {
        FontDir dir = {};
        std::memcpy(&dir, data.data() + sizeof(PackHdr) + f * sizeof(FontDir), sizeof(dir));
        bool found = false;
        for (uint32_t i = 0; i < dir.count; ++i) {
            GlyphEntry e = {};
            std::memcpy(&e, data.data() + dir.idx_off + i * sizeof(GlyphEntry), sizeof(e));
            if (e.cp == test_cps[f]) {
                found = true;
                assert(e.w > 0 && e.h > 0 && e.data_sz > 0 && e.data_sz <= 2048);
                std::vector<uint8_t> out(static_cast<size_t>(e.w) * e.h, 0xCC);
                const uint8_t* src = data.data() + dir.data_base + e.data_off;
                size_t n = (dir.enc == 1)
                               ? study_logic::DecodeRle2ToA8(src, e.data_sz, e.w, e.h, out.data(), e.w)
                               : study_logic::DecodeRaw4ToA8(src, e.data_sz, e.w, e.h, out.data(), e.w);
                assert(n == static_cast<size_t>(e.w) * e.h);
                int nonzero = 0;
                for (uint8_t px : out) {
                    if (px > 0) ++nonzero;
                }
                assert(nonzero > 10);
                break;
            }
        }
        assert(found);
    }
    printf("[PASS] TestSfp2PackAndDecoders (%zu bytes, CRC32=0x%08X)\n",
           data.size(), calc);
}

}  // namespace

int main(int argc, char** argv) {
    TestDisplayRounding();
    TestKeyDecoder();
    TestStateMachine();
    if (argc > 1) {
        TestSfp2PackAndDecoders(argv[1]);
    }
    printf("ALL HOST TESTS PASSED!\n");
    return 0;
}
