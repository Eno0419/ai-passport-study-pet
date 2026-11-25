#ifndef STUDY_FLASH_FONT_H_
#define STUDY_FLASH_FONT_H_

#include <esp_partition.h>
#include <lvgl.h>
#include <cstddef>
#include <cstdint>

// Zero-PSRAM, zero-MMU LVGL font loader reading SFP2 packed fonts directly from
// the `study_fonts` Flash partition via esp_partition_read().
// Internal RAM footprint across all 4 fonts: < 4 KB total.
class StudyFlashFont {
public:
    StudyFlashFont() = default;

    // Bind this instance to a named sub-font ("kai_76", "kai_44", "ui_16", "latin_36")
    // inside the `study_fonts` partition.
    bool Load(const esp_partition_t* part, const char* font_name, const lv_font_t* fallback);

    bool IsLoaded() const { return loaded_; }
    const lv_font_t* GetLvFont() const { return loaded_ ? &font_ : fallback_; }
    uint16_t line_height() const { return line_height_; }
    uint16_t base_line() const { return base_line_; }
    uint32_t glyph_count() const { return glyph_count_; }

private:
    struct __attribute__((packed)) PackHeader {
        char magic[4];
        uint16_t version;
        uint16_t font_count;
        uint32_t total_size;
        uint32_t crc32;
        uint32_t reserved[4];
    };

    struct __attribute__((packed)) FontDirEntry {
        char name[12];
        uint16_t line_height;
        uint16_t base_line;
        uint8_t bpp;
        uint8_t encoding;  // 0 = raw4, 1 = rle2
        uint16_t reserved;
        uint32_t glyph_count;
        uint32_t index_offset;
        uint32_t data_base;
    };

    struct __attribute__((packed)) GlyphEntry {
        uint32_t codepoint;
        uint32_t data_offset;
        uint8_t box_w;
        uint8_t box_h;
        int8_t ofs_x;
        int8_t ofs_y;
        uint8_t adv_w;
        uint8_t reserved;
        uint16_t data_size;
    };

    static constexpr size_t kSkipBuckets = 64;
    static constexpr size_t kScratchBytes = 2048;

    bool ReadGlyphEntry(uint32_t idx, GlyphEntry* out) const;
    int FindGlyphIndex(uint32_t codepoint, GlyphEntry* out) const;

    static bool GetGlyphDscCb(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out,
                              uint32_t unicode_letter, uint32_t unicode_letter_next);
    static const void* GetGlyphBitmapCb(lv_font_glyph_dsc_t* g_dsc, lv_draw_buf_t* draw_buf);

    const esp_partition_t* part_ = nullptr;
    uint32_t index_offset_ = 0;
    uint32_t data_base_ = 0;
    uint32_t glyph_count_ = 0;
    uint16_t line_height_ = 0;
    uint16_t base_line_ = 0;
    uint8_t bpp_ = 0;
    uint8_t encoding_ = 0;
    bool loaded_ = false;

    // Coarse skip-table of codepoints to cut Flash reads during binary search to <= 6
    uint32_t skip_cp_[kSkipBuckets] = {};
    uint32_t skip_idx_[kSkipBuckets] = {};
    size_t skip_len_ = 0;

    lv_font_t font_ = {};
    const lv_font_t* fallback_ = nullptr;

    // Single static scratch buffer shared by all fonts (LVGL renders on one task)
    static uint8_t s_scratch_[kScratchBytes];
};

// Convenience bundle holding all 4 study fonts from the `study_fonts` partition.
class StudyFontPack {
public:
    bool Initialize(const lv_font_t* fallback_font);

    const lv_font_t* kai76() const { return kai_76_.GetLvFont(); }
    const lv_font_t* kai44() const { return kai_44_.GetLvFont(); }
    const lv_font_t* ui16() const { return ui_16_.GetLvFont(); }
    const lv_font_t* latin36() const { return latin_36_.GetLvFont(); }
    bool IsReady() const { return ready_; }

private:
    StudyFlashFont kai_76_;
    StudyFlashFont kai_44_;
    StudyFlashFont ui_16_;
    StudyFlashFont latin_36_;
    bool ready_ = false;
};

#endif  // STUDY_FLASH_FONT_H_
