#include "study_book_nvs.h"

#include <esp_log.h>
#include <nvs.h>
#include <cstring>

#define TAG "StudyBookNvs"

namespace {

constexpr const char* kNvsNamespace = "study_book";
constexpr const char* kNvsKeyBlob = "records_v1";
constexpr uint32_t kBlobMagic = 0x53424B31u;  // "SBK1"
constexpr uint16_t kBlobVersion = 1;

struct __attribute__((packed)) BlobHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint32_t star_count;
    uint32_t crc32;
};

uint8_t ModeToCode(const std::string& mode) {
    if (mode == "phrase") return 1;
    if (mode == "english") return 2;
    return 0;
}

std::string CodeToMode(uint8_t code) {
    if (code == 1) return "phrase";
    if (code == 2) return "english";
    return "hanzi";
}

void AppendStr(std::vector<uint8_t>& buf, const std::string& s, size_t max_len) {
    size_t n = s.size() > max_len ? max_len : s.size();
    buf.push_back(static_cast<uint8_t>(n));
    buf.insert(buf.end(), s.begin(), s.begin() + static_cast<ptrdiff_t>(n));
}

bool ReadStr(const uint8_t* data, size_t len, size_t& pos, std::string& out) {
    if (pos >= len) return false;
    uint8_t n = data[pos++];
    if (pos + n > len) return false;
    out.assign(reinterpret_cast<const char*>(data + pos), n);
    pos += n;
    return true;
}

}  // namespace

bool StudyBookNvs::Load(std::vector<StudyItem>& out_items, uint32_t& out_stars) {
    out_items.clear();
    out_stars = 0;

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return false;
    }

    size_t blob_len = 0;
    err = nvs_get_blob(h, kNvsKeyBlob, nullptr, &blob_len);
    if (err != ESP_OK || blob_len < sizeof(BlobHeader) || blob_len > 6144) {
        nvs_close(h);
        return false;
    }

    std::vector<uint8_t> raw(blob_len);
    err = nvs_get_blob(h, kNvsKeyBlob, raw.data(), &blob_len);
    nvs_close(h);
    if (err != ESP_OK) {
        return false;
    }

    BlobHeader hdr = {};
    std::memcpy(&hdr, raw.data(), sizeof(hdr));
    if (hdr.magic != kBlobMagic || hdr.version != kBlobVersion ||
        hdr.count > StudyStateMachine::kMaxItems) {
        ESP_LOGW(TAG, "Ignoring invalid StudyBook NVS header");
        return false;
    }

    uint32_t calc = study_logic::Crc32(raw.data() + sizeof(BlobHeader),
                                       raw.size() - sizeof(BlobHeader));
    if (calc != hdr.crc32) {
        ESP_LOGW(TAG, "StudyBook NVS CRC mismatch (stored=0x%08lx calc=0x%08lx)",
                 (unsigned long)hdr.crc32, (unsigned long)calc);
        return false;
    }

    size_t pos = sizeof(BlobHeader);
    out_items.reserve(hdr.count);
    for (uint16_t i = 0; i < hdr.count; ++i) {
        if (pos + 3 > raw.size()) {
            out_items.clear();
            return false;
        }
        StudyItem item;
        item.mode = CodeToMode(raw[pos++]);
        uint16_t ask = static_cast<uint16_t>(raw[pos]) |
                       (static_cast<uint16_t>(raw[pos + 1]) << 8);
        pos += 2;
        item.ask_count = ask == 0 ? 1 : ask;
        if (!ReadStr(raw.data(), raw.size(), pos, item.text) ||
            !ReadStr(raw.data(), raw.size(), pos, item.sub) ||
            !ReadStr(raw.data(), raw.size(), pos, item.hint)) {
            out_items.clear();
            return false;
        }
        out_items.push_back(std::move(item));
    }

    out_stars = hdr.star_count;
    ESP_LOGI(TAG, "Loaded %u study items, %lu stars from NVS",
             (unsigned)out_items.size(), (unsigned long)out_stars);
    return true;
}

bool StudyBookNvs::Save(const std::vector<StudyItem>& items, uint32_t stars) {
    std::vector<uint8_t> payload;
    payload.reserve(sizeof(BlobHeader) + items.size() * 64);
    payload.resize(sizeof(BlobHeader), 0);

    const size_t count = items.size() > StudyStateMachine::kMaxItems
                             ? StudyStateMachine::kMaxItems
                             : items.size();
    const size_t start = items.size() - count;

    for (size_t i = start; i < items.size(); ++i) {
        const StudyItem& it = items[i];
        payload.push_back(ModeToCode(it.mode));
        payload.push_back(static_cast<uint8_t>(it.ask_count & 0xFFu));
        payload.push_back(static_cast<uint8_t>((it.ask_count >> 8) & 0xFFu));
        AppendStr(payload, it.text, 48);
        AppendStr(payload, it.sub, 64);
        AppendStr(payload, it.hint, 128);
    }

    BlobHeader hdr = {};
    hdr.magic = kBlobMagic;
    hdr.version = kBlobVersion;
    hdr.count = static_cast<uint16_t>(count);
    hdr.star_count = stars;
    hdr.crc32 = study_logic::Crc32(payload.data() + sizeof(BlobHeader),
                                   payload.size() - sizeof(BlobHeader));
    std::memcpy(payload.data(), &hdr, sizeof(hdr));

    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_blob(h, kNvsKeyBlob, payload.data(), payload.size());
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save StudyBook NVS (%u bytes): %s",
                 (unsigned)payload.size(), esp_err_to_name(err));
        return false;
    }
    return true;
}
