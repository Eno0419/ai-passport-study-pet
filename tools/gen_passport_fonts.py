#!/usr/bin/env python3
"""
Generate packed Flash font binary (SFP2 format) for FoloToy AI Passport Study Pet
(ESP32-C3, 8MB Flash, no PSRAM, no SD card).

Packs 4 fonts into a single study_fonts.bin (fits comfortably inside the 4.125MB
`study_fonts` Flash partition):
  - kai_76   : 76px Kaiti SC Regular, 2bpp RLE (single Hanzi in 144x144 Tianzige)
  - kai_44   : 44px Kaiti SC Regular, 2bpp RLE (2-4 char phrase in Tianzige grid)
  - ui_16    : 16px crisp UI font,   2bpp RLE (Pinyin with tones, IPA, hints, UI)
  - latin_36 : 36px clean sans-serif, 4bpp raw (English words in 4-line staff)

Also verifies CRC32, sorted index, mandatory primary-school / Pinyin / IPA coverage,
and bit-exact round-trip decode of sampled glyphs.
"""

import argparse
import os
import struct
import sys
import zlib
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

PACK_MAGIC = b"SFP2"
PACK_VERSION = 2
# Pack header: magic(4s), version(H), font_count(H), total_size(I), crc32(I), reserved(4I) = 32B
PACK_HDR_STRUCT = struct.Struct("<4sHHII4I")
# Font dir entry: name(12s), lh(H), bl(H), bpp(B), enc(B), rsv(H), count(I), idx_off(I), data_base(I) = 32B
FONT_DIR_STRUCT = struct.Struct("<12sHHBBHIII")
# Glyph index entry: cp(I), data_off(I), w(B), h(B), ofs_x(b), ofs_y(b), adv_w(B), rsv(B), data_sz(H) = 16B
GLYPH_ENTRY_STRUCT = struct.Struct("<IIBBbbBBH")

ENC_RAW = 0
ENC_RLE2 = 1

KAITI_CANDIDATES = [
    "/System/Library/AssetsV2/com_apple_MobileAsset_Font8/88d6cc32a907955efa1d014207889413890573be.asset/AssetData/Kaiti.ttc",
    "/System/Library/Fonts/Supplemental/Songti.ttc",
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/usr/share/fonts/truetype/lxgw-wenkai/LXGWWenKai-Regular.ttf",
    "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
    "C:/Windows/Fonts/simkai.ttf",
]
HEITI_CANDIDATES = [
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/System/Library/Fonts/PingFang.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "C:/Windows/Fonts/msyh.ttc",
]
ARIAL_UNI_CANDIDATES = [
    "/Library/Fonts/Arial Unicode.ttf",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "/System/Library/Fonts/HelveticaNeue.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "C:/Windows/Fonts/arial.ttf",
]
HELVETICA_CANDIDATES = [
    "/System/Library/Fonts/HelveticaNeue.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "C:/Windows/Fonts/arial.ttf",
]


def pick_font_path(explicit: str | None, candidates: list[str], label: str) -> str:
    if explicit:
        if not os.path.exists(explicit):
            raise FileNotFoundError(f"Specified {label} font does not exist: {explicit}")
        return explicit
    for p in candidates:
        if os.path.exists(p):
            return p
    raise FileNotFoundError(
        f"Cannot find {label} font automatically. Pass --{label.lower()}-font <path/to/font.ttf>. "
        f"Checked: {candidates}"
    )


def collect_hanzi_chars() -> list[int]:
    """Collect GB2312 Level-1 (3,755 chars) + common primary-school Level-2 extras."""
    chars = set()
    for qu in range(16, 56):
        for wei in range(1, 95):
            if qu == 55 and wei >= 90:
                continue
            b = bytes([0xA0 + qu, 0xA0 + wei])
            try:
                ch = b.decode("gb2312")
                if len(ch) == 1:
                    chars.add(ord(ch))
            except UnicodeDecodeError:
                pass

    extra = (
        "啰嗦尴尬朦胧旖旎蜿蜒崎岖蹒跚踉跄翱翔璀璨绚丽斑斓婀娜"
        "铿锵嘀咕唠叨喃喃嘟囔哼哧噗嗤咔嚓哗啦噼啪嗡嗡吱呀喵汪"
        "草莓葡萄樱桃柠檬橘子柚子榴莲芒果猕猴桃菠萝蜜香蕉苹果"
        "蜻蜓蝴蝶蟋蟀螳螂蜈蚣蜘蛛蝌蚪青蛙螃蟹乌龟鲨鱼鲸鱼海豚"
        "鹦鹉孔雀鸳鸯喜鹊乌鸦啄木鸟猫头鹰袋鼠企鹅长颈鹿斑马"
        "橡皮铅笔蜡笔彩笔剪刀尺子书包课本作业本练习册文具盒"
        "跳绳毽子风筝陀螺秋千滑梯跷跷板积木拼图魔方围棋象棋"
        "微笑开心高兴快乐难过伤心生气惊讶害怕害羞好奇加油棒"
        "丨丿丶乙亅亻氵扌木火土金日月中大人口手山水田"
        "亠冫冖凵刂勹匕匚匸卜卩厂厶又囗夂夊宀寸尢尸屮巛廴廾弋弓彐彡彳"
        "忄攵灬爫犭礻糹罒耂肀艹虍衤讠辶阝飠钅饣纟讠⺗⺡⺣⺨⺩⺫⺭⺮⺼⺾⻂⻊⻌⻏⻖"
        "龺艹竹雨虫鸟鱼马牛羊车舟衣食住行父母兄妹姐弟"
        "写好了上一个下一个生字本桌面返回重读拼读笔顺部首组词例句"
        "按住说话点击翻页长按追问星星复习听写全部历史无记录"
    )
    for ch in extra:
        chars.add(ord(ch))
    return sorted(chars)


def collect_latin_and_symbols() -> list[int]:
    chars = set(range(0x20, 0x7F))
    # Pinyin tone vowels (lower + upper)
    pinyin = "āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜüĀÁǍÀĒÉĚÈĪÍǏÌŌÓǑÒŪÚǓÙǕǗǙǛÜńňǹɡ"
    # Common IPA phonetic symbols
    ipa = "ˈˌːæɑɒɔəɛɜɪʊʌθðʃʒŋʧʤɹɾɫɐɜː·"
    # CJK punctuation + UI arrows/symbols + CJK strokes
    punct = "，。、！？；：（）【】《》“”‘’—…·　◀▶▲▼●○★☆✓✕×㇐㇑㇓㇔㇏㇕㇖㇗㇚"
    for s in (pinyin, ipa, punct):
        for ch in s:
            chars.add(ord(ch))
    return sorted(chars)


def quantize_2bpp(alpha: int) -> int:
    # 0..255 -> 0..3 with rounded thresholds (42, 127, 212)
    if alpha < 43:
        return 0
    if alpha < 128:
        return 1
    if alpha < 213:
        return 2
    return 3


def encode_rle2(pixels_2bpp: list[int]) -> bytes:
    """
    3-mode 2bpp RLE codec:
      - 0xC0..0xFF : zero run of length ((b & 0x3F) + 1) * 4  (4..256 zeros)
      - 0x00..0x7F : run of val=(b>>5)&3, length=(b&0x1F)+1   (1..32 pixels)
      - 0x80..0xBF : 3 literal 2bpp pixels (p0, p1, p2)
    """
    out = bytearray()
    n = len(pixels_2bpp)
    i = 0
    while i < n:
        v = pixels_2bpp[i]
        # Count run length of value v
        run = 1
        while i + run < n and pixels_2bpp[i + run] == v and run < 256:
            run += 1

        if v == 0 and run >= 4:
            # Emit long zero runs in multiples of 4 (up to 256)
            chunks = min(64, run // 4)
            out.append(0xC0 | (chunks - 1))
            i += chunks * 4
            continue

        if run >= 3:
            take = min(32, run)
            out.append((v << 5) | (take - 1))
            i += take
            continue

        # Short run (1 or 2): check if literal-3 is better than separate runs
        if i + 2 < n:
            # If next pixel starts a long run (>=4), emit this single/double as short run
            v1 = pixels_2bpp[i + 1]
            v2 = pixels_2bpp[i + 2]
            if run == 1 and v1 == v2:
                # Peek if v1 runs >= 3
                r1 = 2
                while i + 1 + r1 < n and pixels_2bpp[i + 1 + r1] == v1 and r1 < 4:
                    r1 += 1
                if r1 >= 3:
                    out.append((v << 5) | 0)  # len=1
                    i += 1
                    continue
            out.append(0x80 | (v << 4) | (v1 << 2) | v2)
            i += 3
        else:
            # Tail of 1 or 2 pixels
            out.append((v << 5) | (run - 1))
            i += run
    return bytes(out)


def decode_rle2(data: bytes, total_pixels: int) -> list[int]:
    out = []
    for b in data:
        if len(out) >= total_pixels:
            break
        if (b & 0xC0) == 0xC0:
            cnt = ((b & 0x3F) + 1) * 4
            need = min(cnt, total_pixels - len(out))
            out.extend([0] * need)
        elif (b & 0x80) == 0x80:
            for shift in (4, 2, 0):
                if len(out) < total_pixels:
                    out.append((b >> shift) & 0x03)
        else:
            val = (b >> 5) & 0x03
            cnt = (b & 0x1F) + 1
            need = min(cnt, total_pixels - len(out))
            out.extend([val] * need)
    return out


def encode_raw4(alpha_pixels: list[int], w: int, h: int) -> bytes:
    """Row-aligned 4bpp packed (high nibble first, matches LVGL & SFT1)."""
    out = bytearray()
    idx = 0
    for _ in range(h):
        x = 0
        while x + 1 < w:
            p0 = alpha_pixels[idx] >> 4
            p1 = alpha_pixels[idx + 1] >> 4
            out.append((p0 << 4) | p1)
            idx += 2
            x += 2
        if x < w:
            p0 = alpha_pixels[idx] >> 4
            out.append(p0 << 4)
            idx += 1
    return bytes(out)


def render_font_blob(
    name: str,
    primary_font_path: str,
    fallback_font_paths: list[str],
    px_size: int,
    codepoints: list[int],
    bpp: int,
    use_rle2: bool,
) -> dict:
    primary = ImageFont.truetype(primary_font_path, px_size)
    fallbacks = [ImageFont.truetype(p, px_size) for p in fallback_font_paths]
    ascent, descent = primary.getmetrics()
    line_height = ascent + descent
    base_line = descent

    canvas_w = max(px_size * 2, 64)
    canvas_h = max(line_height * 2, 64)
    origin_x = px_size // 2
    origin_y = px_size // 2

    entries = []
    data_blob = bytearray()
    sample_verify = []

    for cp in sorted(set(codepoints)):
        ch = chr(cp)
        # Choose primary font if it renders non-empty (or space), else fallback
        chosen_font = primary
        if ch not in (" ", "\u3000"):
            mask = primary.getmask(ch)
            if mask.size[0] == 0 or mask.size[1] == 0:
                for fb in fallbacks:
                    fb_mask = fb.getmask(ch)
                    if fb_mask.size[0] > 0 and fb_mask.size[1] > 0:
                        chosen_font = fb
                        break

        if ch in (" ", "\u3000"):
            adv = int(round(chosen_font.getlength(ch)))
            if ch == "\u3000" and adv < px_size // 2:
                adv = px_size
            adv = max(1, min(255, adv))
            entries.append((cp, len(data_blob), 0, 0, 0, 0, adv, 0, 0))
            continue

        img = Image.new("L", (canvas_w, canvas_h), 0)
        draw = ImageDraw.Draw(img)
        draw.text((origin_x, origin_y), ch, fill=255, font=chosen_font)
        bbox = img.getbbox()
        adv = int(round(chosen_font.getlength(ch)))
        adv = max(1, min(255, adv))

        if not bbox:
            entries.append((cp, len(data_blob), 0, 0, 0, 0, adv, 0, 0))
            continue

        x0, y0, x1, y1 = bbox
        bw = x1 - x0
        bh = y1 - y0
        if bw > 255 or bh > 255:
            # Clamp pathological glyphs defensively
            x1 = min(x1, x0 + 255)
            y1 = min(y1, y0 + 255)
            bw = x1 - x0
            bh = y1 - y0

        ofs_x = x0 - origin_x
        # LVGL ofs_y is distance from baseline to bottom of glyph box (positive = above baseline)
        baseline_y = origin_y + ascent
        ofs_y = baseline_y - y1
        ofs_x = max(-128, min(127, ofs_x))
        ofs_y = max(-128, min(127, ofs_y))

        crop = img.crop((x0, y0, x1, y1))
        raw_alpha = list(crop.tobytes())

        if bpp == 2 and use_rle2:
            pix2 = [quantize_2bpp(a) for a in raw_alpha]
            enc = encode_rle2(pix2)
            # Round-trip sanity check on sample characters
            if ch in ("勇", "蝶", "赢", "ā", "ǚ", "g", "W") or len(sample_verify) < 16:
                dec = decode_rle2(enc, bw * bh)
                if dec != pix2:
                    raise RuntimeError(f"RLE2 round-trip mismatch on {name} U+{cp:04X}")
                sample_verify.append(cp)
        elif bpp == 4 and not use_rle2:
            enc = encode_raw4(raw_alpha, bw, bh)
        else:
            raise ValueError(f"Unsupported bpp={bpp} use_rle2={use_rle2}")

        if len(enc) > 65535:
            raise RuntimeError(f"Glyph too large: {name} U+{cp:04X} ({len(enc)} B)")

        data_offset = len(data_blob)
        data_blob.extend(enc)
        entries.append((cp, data_offset, bw, bh, ofs_x, ofs_y, adv, 0, len(enc)))

    return {
        "name": name,
        "line_height": line_height,
        "base_line": base_line,
        "bpp": bpp,
        "encoding": ENC_RLE2 if use_rle2 else ENC_RAW,
        "entries": entries,
        "data": bytes(data_blob),
    }


def build_pack(
    output_path: Path,
    max_bytes: int,
    kaiti_font: str | None = None,
    ui_font: str | None = None,
    latin_font: str | None = None,
    ipa_font: str | None = None,
) -> None:
    kaiti_path = pick_font_path(kaiti_font, KAITI_CANDIDATES, "kaiti")
    heiti_path = pick_font_path(ui_font, HEITI_CANDIDATES, "ui")
    arial_uni_path = pick_font_path(ipa_font, ARIAL_UNI_CANDIDATES, "ipa")
    helvetica_path = pick_font_path(latin_font, HELVETICA_CANDIDATES, "latin")

    hanzi = collect_hanzi_chars()
    latin_sym = collect_latin_and_symbols()
    all_ui = sorted(set(hanzi) | set(latin_sym))

    print(f"Fonts: Kaiti={kaiti_path}")
    print(f"       Heiti={heiti_path}")
    print(f"       Latin={helvetica_path}, IPA={arial_uni_path}")
    print(f"Char counts: hanzi={len(hanzi)}, latin_sym={len(latin_sym)}, ui={len(all_ui)}")

    specs = [
        ("kai_76",   kaiti_path,     [heiti_path, arial_uni_path], 76, hanzi,     2, True),
        ("kai_44",   kaiti_path,     [heiti_path, arial_uni_path], 44, hanzi,     2, True),
        ("ui_16",    heiti_path,     [arial_uni_path, helvetica_path], 16, all_ui, 2, True),
        ("latin_36", helvetica_path, [arial_uni_path],             36, latin_sym, 4, False),
    ]

    blobs = []
    for name, primary, fallbacks, px, cps, bpp, rle in specs:
        b = render_font_blob(name, primary, fallbacks, px, cps, bpp, rle)
        idx_sz = len(b["entries"]) * GLYPH_ENTRY_STRUCT.size
        dat_sz = len(b["data"])
        print(f"  [{name:8s}] px={px:2d} bpp={bpp} enc={'RLE2' if rle else 'RAW4'} "
              f"glyphs={len(b['entries']):4d} lh={b['line_height']:2d} bl={b['base_line']:2d} "
              f"idx={idx_sz/1024:5.1f}KB data={dat_sz/1024:6.1f}KB total={(idx_sz+dat_sz)/1024:6.1f}KB")
        blobs.append(b)

    # Assemble SFP2 pack
    header_size = PACK_HDR_STRUCT.size
    dir_size = len(blobs) * FONT_DIR_STRUCT.size
    cur_off = header_size + dir_size

    dir_bytes = bytearray()
    body_bytes = bytearray()

    for b in blobs:
        # Align each section to 4 bytes
        pad = (-cur_off) & 3
        if pad:
            body_bytes.extend(b"\x00" * pad)
            cur_off += pad
        index_offset = cur_off
        idx_buf = bytearray()
        for e in b["entries"]:
            idx_buf.extend(GLYPH_ENTRY_STRUCT.pack(*e))
        body_bytes.extend(idx_buf)
        cur_off += len(idx_buf)

        pad = (-cur_off) & 3
        if pad:
            body_bytes.extend(b"\x00" * pad)
            cur_off += pad
        data_base = cur_off
        body_bytes.extend(b["data"])
        cur_off += len(b["data"])

        name_raw = b["name"].encode("ascii")[:11].ljust(12, b"\x00")
        dir_bytes.extend(FONT_DIR_STRUCT.pack(
            name_raw,
            b["line_height"],
            b["base_line"],
            b["bpp"],
            b["encoding"],
            0,
            len(b["entries"]),
            index_offset,
            data_base,
        ))

    payload_after_crc = bytes(PACK_HDR_STRUCT.pack(PACK_MAGIC, PACK_VERSION, len(blobs), cur_off, 0, 0, 0, 0, 0)[16:]) + bytes(dir_bytes) + bytes(body_bytes)
    crc = zlib.crc32(payload_after_crc) & 0xFFFFFFFF
    header = PACK_HDR_STRUCT.pack(PACK_MAGIC, PACK_VERSION, len(blobs), cur_off, crc, 0, 0, 0, 0)
    full_pack = header + bytes(dir_bytes) + bytes(body_bytes)
    assert len(full_pack) == cur_off

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(full_pack)
    print(f"Wrote {output_path}: {len(full_pack):,} bytes ({len(full_pack)/1024/1024:.2f} MB), CRC32=0x{crc:08X}")
    if len(full_pack) > max_bytes:
        raise RuntimeError(f"Pack exceeds partition limit: {len(full_pack)} > {max_bytes}")


def verify_pack(pack_path: Path) -> None:
    data = pack_path.read_bytes()
    magic, ver, count, total_sz, stored_crc, *_ = PACK_HDR_STRUCT.unpack_from(data, 0)
    assert magic == PACK_MAGIC, f"Bad magic: {magic}"
    assert ver == PACK_VERSION, f"Bad version: {ver}"
    assert total_sz == len(data), f"Size mismatch: {total_sz} != {len(data)}"
    calc_crc = zlib.crc32(data[16:]) & 0xFFFFFFFF
    assert calc_crc == stored_crc, f"CRC mismatch: 0x{calc_crc:08X} != 0x{stored_crc:08X}"

    required_by_font = {
        "kai_76":   "勇蝴蝶赢暖餐一二三上下左右大小人口手山水田",
        "kai_44":   "蝴蝶勇敢开心学习写字词语拼音",
        "ui_16":    "āáǎàēéěèīíǐìōóǒòūúǔùǖǘǚǜˈˌːæəɪʊʌ写好了上一个生字本返回部首组词",
        "latin_36": "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyzbutterfly0123456789/ˈbʌtəflaɪ/",
    }

    for i in range(count):
        off = PACK_HDR_STRUCT.size + i * FONT_DIR_STRUCT.size
        name_raw, lh, bl, bpp, enc, _, gcnt, idx_off, data_base = FONT_DIR_STRUCT.unpack_from(data, off)
        name = name_raw.split(b"\x00", 1)[0].decode("ascii")
        # Verify strictly ascending codepoints
        prev_cp = -1
        cp_map = {}
        for j in range(gcnt):
            e = GLYPH_ENTRY_STRUCT.unpack_from(data, idx_off + j * GLYPH_ENTRY_STRUCT.size)
            cp = e[0]
            assert cp > prev_cp, f"{name}: unsorted codepoint U+{cp:04X}"
            prev_cp = cp
            cp_map[cp] = e

        for ch in required_by_font.get(name, ""):
            cp = ord(ch)
            assert cp in cp_map, f"{name}: missing required char '{ch}' (U+{cp:04X})"
            _, d_off, bw, bh, _, _, adv, _, d_sz = cp_map[cp]
            if bw > 0 and bh > 0:
                blob = data[data_base + d_off : data_base + d_off + d_sz]
                if enc == ENC_RLE2:
                    pix = decode_rle2(blob, bw * bh)
                    assert len(pix) == bw * bh, f"{name}: decode len mismatch for '{ch}'"
                elif enc == ENC_RAW and bpp == 4:
                    row_bytes = (bw + 1) // 2
                    assert len(blob) == row_bytes * bh, f"{name}: raw4 len mismatch for '{ch}'"
        print(f"  [VERIFY OK] {name}: {gcnt} glyphs, lh={lh}, bl={bl}, all required chars decoded cleanly")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--output", default="study_assets/study_fonts.bin")
    ap.add_argument("--max-bytes", type=int, default=0x420000)
    ap.add_argument("--kaiti-font", default=None, help="Path to KaiTi/serif CJK .ttf/.ttc (e.g. LXGWWenKai-Regular.ttf)")
    ap.add_argument("--ui-font", default=None, help="Path to UI sans-serif CJK .ttf/.ttc (e.g. NotoSansSC-Regular.ttf)")
    ap.add_argument("--latin-font", default=None, help="Path to Latin sans-serif .ttf/.ttc for English words")
    ap.add_argument("--ipa-font", default=None, help="Path to IPA/Unicode fallback .ttf/.ttc")
    ap.add_argument("--verify-only", action="store_true")
    args = ap.parse_args()

    out = Path(args.output)
    if not args.verify_only:
        build_pack(
            out,
            args.max_bytes,
            kaiti_font=args.kaiti_font,
            ui_font=args.ui_font,
            latin_font=args.latin_font,
            ipa_font=args.ipa_font,
        )
    verify_pack(out)


if __name__ == "__main__":
    main()
