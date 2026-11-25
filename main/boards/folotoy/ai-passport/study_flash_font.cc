#include "study_flash_font.h"
#include "study_logic.h"

#include <esp_log.h>
#include <cstring>

#define TAG "StudyFlashFont"

uint8_t StudyFlashFont::s_scratch_[StudyFlashFont::kScratchBytes] = {};

bool StudyFlashFont::ReadGlyphEntry(uint32_t idx, GlyphEntry* out) const {
    if (!part_ || !out || idx >= glyph_count_) {
        return false;
    }
    const size_t off = index_offset_ + idx * sizeof(GlyphEntry);
    return esp_partition_read(part_, off, out, sizeof(GlyphEntry)) == ESP_OK;
}

bool StudyFlashFont::Load(const esp_partition_t* part, const char* font_name,
                          const lv_font_t* fallback) {
    loaded_ = false;
    part_ = part;
    fallback_ = fallback;
    skip_len_ = 0;

    if (!part_ || !font_name) {
        return false;
    }

    PackHeader hdr = {};
    if (esp_partition_read(part_, 0, &hdr, sizeof(hdr)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SFP2 pack header");
        return false;
    }
    if (std::memcmp(hdr.magic, "SFP2", 4) != 0 || hdr.version != 2 || hdr.font_count == 0 ||
        hdr.total_size > part_->size) {
        ESP_LOGW(TAG, "Invalid SFP2 header (magic=%.4s ver=%u cnt=%u sz=%lu)",
                 hdr.magic, hdr.version, hdr.font_count, (unsigned long)hdr.total_size);
        return false;
    }

    FontDirEntry dir = {};
    bool found = false;
    for (uint16_t i = 0; i < hdr.font_count && i < 16; ++i) {
        const size_t dir_off = sizeof(PackHeader) + i * sizeof(FontDirEntry);
        if (esp_partition_read(part_, dir_off, &dir, sizeof(dir)) != ESP_OK) {
            return false;
        }
        if (std::strncmp(dir.name, font_name, sizeof(dir.name)) == 0) {
            found = true;
            break;
        }
    }
    if (!found || dir.glyph_count == 0) {
        ESP_LOGW(TAG, "Font '%s' not found in SFP2 pack", font_name);
        return false;
    }

    index_offset_ = dir.index_offset;
    data_base_ = dir.data_base;
    glyph_count_ = dir.glyph_count;
    line_height_ = dir.line_height;
    base_line_ = dir.base_line;
    bpp_ = dir.bpp;
    encoding_ = dir.encoding;

    // Build coarse skip table (at most kSkipBuckets samples across the sorted index)
    const size_t samples = glyph_count_ < kSkipBuckets ? glyph_count_ : kSkipBuckets;
    for (size_t i = 0; i < samples; ++i) {
        uint32_t idx = (samples <= 1)
                           ? 0
                           : static_cast<uint32_t>((static_cast<uint64_t>(i) * (glyph_count_ - 1)) /
                                                   (samples - 1));
        GlyphEntry e = {};
        if (!ReadGlyphEntry(idx, &e)) {
            return false;
        }
        skip_idx_[i] = idx;
        skip_cp_[i] = e.codepoint;
    }
    skip_len_ = samples;

    std::memset(&font_, 0, sizeof(font_));
    font_.get_glyph_dsc = GetGlyphDscCb;
    font_.get_glyph_bitmap = GetGlyphBitmapCb;
    font_.line_height = line_height_;
    font_.base_line = base_line_;
    font_.subpx = LV_FONT_SUBPX_NONE;
    font_.dsc = this;
    font_.fallback = fallback_;
    loaded_ = true;

    ESP_LOGI(TAG, "Loaded Flash font '%s': glyphs=%lu lh=%u bl=%u bpp=%u enc=%u",
             font_name, (unsigned long)glyph_count_, line_height_, base_line_, bpp_, encoding_);
    return true;
}

int StudyFlashFont::FindGlyphIndex(uint32_t codepoint, GlyphEntry* out) const {
    if (!loaded_ || glyph_count_ == 0 || skip_len_ == 0) {
        return -1;
    }
    if (codepoint < skip_cp_[0] || codepoint > skip_cp_[skip_len_ - 1]) {
        return -1;
    }

    // Narrow search range in RAM using coarse skip table
    int lo = 0;
    int hi = static_cast<int>(glyph_count_) - 1;
    size_t slo = 0;
    size_t shi = skip_len_ - 1;
    while (slo + 1 < shi) {
        size_t smid = slo + ((shi - slo) >> 1);
        if (skip_cp_[smid] == codepoint) {
            if (ReadGlyphEntry(skip_idx_[smid], out)) {
                return static_cast<int>(skip_idx_[smid]);
            }
            return -1;
        }
        if (skip_cp_[smid] < codepoint) {
            slo = smid;
        } else {
            shi = smid;
        }
    }
    lo = static_cast<int>(skip_idx_[slo]);
    hi = static_cast<int>(skip_idx_[shi]);

    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        GlyphEntry e = {};
        if (!ReadGlyphEntry(static_cast<uint32_t>(mid), &e)) {
            return -1;
        }
        if (e.codepoint == codepoint) {
            if (out) *out = e;
            return mid;
        }
        if (e.codepoint < codepoint) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

bool StudyFlashFont::GetGlyphDscCb(const lv_font_t* font, lv_font_glyph_dsc_t* dsc_out,
                                   uint32_t unicode_letter, uint32_t unicode_letter_next) {
    LV_UNUSED(unicode_letter_next);
    if (!font || !dsc_out || !font->dsc) {
        return false;
    }
    const auto* self = static_cast<const StudyFlashFont*>(font->dsc);
    if (!self->loaded_) {
        return false;
    }
    if (unicode_letter == '\t') {
        unicode_letter = ' ';
    }

    GlyphEntry e = {};
    int idx = self->FindGlyphIndex(unicode_letter, &e);
    if (idx < 0) {
        return false;
    }

    dsc_out->adv_w = e.adv_w;
    dsc_out->box_w = e.box_w;
    dsc_out->box_h = e.box_h;
    dsc_out->ofs_x = e.ofs_x;
    dsc_out->ofs_y = e.ofs_y;
    dsc_out->stride = 0;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc_out->is_placeholder = false;
    dsc_out->gid.index = static_cast<uint32_t>(idx + 1);
    dsc_out->resolved_font = font;
    return true;
}

const void* StudyFlashFont::GetGlyphBitmapCb(lv_font_glyph_dsc_t* g_dsc, lv_draw_buf_t* draw_buf) {
    if (!g_dsc || !draw_buf || !g_dsc->resolved_font || !g_dsc->resolved_font->dsc) {
        return nullptr;
    }
    const auto* self = static_cast<const StudyFlashFont*>(g_dsc->resolved_font->dsc);
    const uint32_t gid = g_dsc->gid.index;
    if (!self->loaded_ || gid == 0 || gid > self->glyph_count_) {
        return nullptr;
    }

    GlyphEntry e = {};
    if (!self->ReadGlyphEntry(gid - 1, &e)) {
        return nullptr;
    }
    if (e.box_w == 0 || e.box_h == 0 || e.data_size == 0) {
        return nullptr;
    }
    if (e.data_size > kScratchBytes) {
        ESP_LOGW(TAG, "Glyph data too big for scratch: %u > %u", e.data_size, (unsigned)kScratchBytes);
        return nullptr;
    }

    const size_t flash_off = self->data_base_ + e.data_offset;
    if (esp_partition_read(self->part_, flash_off, s_scratch_, e.data_size) != ESP_OK) {
        return nullptr;
    }

    const uint32_t stride_out = draw_buf->header.stride
                                    ? draw_buf->header.stride
                                    : lv_draw_buf_width_to_stride(e.box_w, LV_COLOR_FORMAT_A8);
    if (self->encoding_ == 1 && self->bpp_ == 2) {
        study_logic::DecodeRle2ToA8(s_scratch_, e.data_size, e.box_w, e.box_h,
                                    draw_buf->data, stride_out);
    } else if (self->encoding_ == 0 && self->bpp_ == 4) {
        study_logic::DecodeRaw4ToA8(s_scratch_, e.data_size, e.box_w, e.box_h,
                                    draw_buf->data, stride_out);
    } else {
        return nullptr;
    }

    lv_draw_buf_flush_cache(draw_buf, nullptr);
    return draw_buf;
}

bool StudyFontPack::Initialize(const lv_font_t* fallback_font) {
    const lv_font_t* fb = fallback_font ? fallback_font : LV_FONT_DEFAULT;
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "study_fonts");
    if (!part) {
        ESP_LOGW(TAG, "Partition 'study_fonts' not found; falling back to built-in font");
        ready_ = false;
        return false;
    }

    bool ok_ui = ui_16_.Load(part, "ui_16", fb);
    const lv_font_t* ui_ref = ui_16_.GetLvFont();
    bool ok_44 = kai_44_.Load(part, "kai_44", ui_ref);
    bool ok_76 = kai_76_.Load(part, "kai_76", kai_44_.GetLvFont());
    bool ok_lat = latin_36_.Load(part, "latin_36", ui_ref);

    ready_ = ok_ui || ok_44 || ok_76 || ok_lat;
    ESP_LOGI(TAG, "StudyFontPack ready=%d (kai76=%d kai44=%d ui16=%d latin36=%d)",
             ready_, ok_76, ok_44, ok_ui, ok_lat);
    return ready_;
}
