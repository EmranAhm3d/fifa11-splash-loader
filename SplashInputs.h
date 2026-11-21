// SplashInputs.h - decoders for the non-DDS inputs: FSH (raw / RefPack / chunkzip) and PNG / JPG.
// Everything ends up in splash::Image (DXT1 blocks to pass through, or RGBA8 to encode).
// Platform-independent. Define STB_IMAGE_IMPLEMENTATION in exactly one .cpp (Main.cpp does).
#pragma once
#include "SplashCore.h"

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "vendor/stb_image.h"

namespace splash {

enum class FileKind { Unknown, Fsh, Dds, Png, Jpg };

inline const char *KindName(FileKind k) {
    switch (k) {
    case FileKind::Fsh: return "FSH";
    case FileKind::Dds: return "DDS";
    case FileKind::Png: return "PNG";
    case FileKind::Jpg: return "JPG";
    default: return "unknown";
    }
}

inline uint32_t Rd32BE(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
inline uint32_t Rd24(const uint8_t *p) { return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16); }

inline bool IsRefPack(const uint8_t *d, size_t n) { return n >= 5 && (d[0] & 0x3E) == 0x10 && d[1] == 0xFB; }

// Identify a file by its content (the extension only decides which file gets picked).
inline FileKind Sniff(const uint8_t *d, size_t n) {
    if (n >= 4 && !memcmp(d, "DDS ", 4)) return FileKind::Dds;
    if (n >= 4 && !memcmp(d, "SHPI", 4)) return FileKind::Fsh;
    if (n >= 8 && (!memcmp(d, "chunkzip", 8) || !memcmp(d, "chunkref", 8) || !memcmp(d, "chunklzx", 8))) return FileKind::Fsh;
    if (IsRefPack(d, n)) return FileKind::Fsh;
    if (n >= 8 && !memcmp(d, "\x89PNG\r\n\x1a\n", 8)) return FileKind::Png;
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return FileKind::Jpg;
    return FileKind::Unknown;
}

// ---- RefPack (QFS) decompression ----
inline std::string DecompressRefPack(const uint8_t *in, size_t n, std::vector<uint8_t> &out) {
    size_t p = 2;
    bool big = (in[0] & 0x80) != 0;
    size_t fieldLen = big ? 4 : 3;
    if (in[0] & 0x01) p += fieldLen; // compressed size present
    if (p + fieldLen > n) return "the RefPack header is truncated";
    size_t size = 0;
    for (size_t i = 0; i < fieldLen; i++) size = (size << 8) | in[p + i];
    p += fieldLen;
    if (size == 0 || size > 128u * 1024u * 1024u) return "the RefPack size is invalid";
    out.assign(size, 0);
    size_t o = 0;
    auto lit = [&](size_t count) -> bool {
        if (p + count > n || o + count > size) return false;
        memcpy(&out[o], in + p, count); p += count; o += count; return true;
    };
    auto back = [&](size_t off, size_t count) -> bool {
        if (off > o || o + count > size) return false;
        for (size_t i = 0; i < count; i++, o++) out[o] = out[o - off];
        return true;
    };
    while (p < n) {
        uint8_t b0 = in[p];
        if (b0 < 0x80) {
            if (p + 2 > n) return "the RefPack stream is truncated";
            uint8_t b1 = in[p + 1]; p += 2;
            if (!lit(b0 & 3) || !back(((b0 & 0x60u) << 3) + b1 + 1, ((b0 >> 2) & 7) + 3)) return "the RefPack stream is corrupted";
        } else if (b0 < 0xC0) {
            if (p + 3 > n) return "the RefPack stream is truncated";
            uint8_t b1 = in[p + 1], b2 = in[p + 2]; p += 3;
            if (!lit(b1 >> 6) || !back(((b1 & 0x3Fu) << 8) + b2 + 1, (b0 & 0x3F) + 4)) return "the RefPack stream is corrupted";
        } else if (b0 < 0xE0) {
            if (p + 4 > n) return "the RefPack stream is truncated";
            uint8_t b1 = in[p + 1], b2 = in[p + 2], b3 = in[p + 3]; p += 4;
            if (!lit(b0 & 3) || !back(((b0 & 0x10u) << 12) + (uint32_t(b1) << 8) + b2 + 1, ((b0 & 0x0Cu) << 6) + b3 + 5))
                return "the RefPack stream is corrupted";
        } else if (b0 < 0xFC) {
            p += 1;
            if (!lit(((b0 & 0x1Fu) + 1) * 4)) return "the RefPack stream is corrupted";
        } else {
            p += 1;
            if (!lit(b0 & 3)) return "the RefPack stream is corrupted";
            break;
        }
    }
    if (o != size) return "the RefPack stream ended early";
    return std::string();
}

// ---- EA chunkzip (as used for the splash inside fifa.exe) ----
inline std::string DecompressChunkzip(const uint8_t *in, size_t n, std::vector<uint8_t> &out) {
    if (n < 0x28) return "the chunkzip header is truncated";
    if (memcmp(in, "chunkzip", 8)) return "chunkref/chunklzx compression is not supported (only chunkzip)";
    uint32_t usize = Rd32BE(in + 12), count = Rd32BE(in + 20), align = Rd32BE(in + 24);
    if (usize == 0 || usize > 128u * 1024u * 1024u || count == 0 || count > 4096) return "the chunkzip header is invalid";
    if (align == 0 || (align & (align - 1))) align = 1;
    out.clear();
    out.reserve(usize);
    size_t p = 0x28;
    for (uint32_t i = 0; i < count; i++) {
        p = ((p + 8 + align - 1) & ~size_t(align - 1)) - 8; // chunk data starts on an 'align' boundary
        if (p + 8 > n) return "the chunkzip stream is truncated";
        uint32_t clen = Rd32BE(in + p), type = Rd32BE(in + p + 4);
        p += 8;
        if (clen > n - p) return "the chunkzip stream is truncated";
        if (type == 1) {
            size_t room = usize - out.size();
            std::vector<uint8_t> buf(room ? room : 1);
            int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char *>(buf.data()), int(buf.size()),
                                                       reinterpret_cast<const char *>(in + p), int(clen));
            if (got < 0) return "a chunkzip chunk failed to decompress";
            out.insert(out.end(), buf.begin(), buf.begin() + got);
        } else if (type == 0) {
            out.insert(out.end(), in + p, in + p + clen); // stored
        } else
            return "unsupported chunkzip chunk type " + std::to_string(type);
        p += clen;
    }
    if (out.size() != usize) return "the chunkzip data has the wrong size";
    return std::string();
}

// ---- FSH ----
inline std::string ParseFSH(const uint8_t *f, size_t n, Image &out) {
    std::vector<uint8_t> unpacked;
    if (n >= 8 && !memcmp(f, "chunk", 5)) {
        auto e = DecompressChunkzip(f, n, unpacked);
        if (!e.empty()) return "The FSH could not be unpacked: " + e + ".";
        f = unpacked.data(); n = unpacked.size();
    } else if (IsRefPack(f, n)) {
        auto e = DecompressRefPack(f, n, unpacked);
        if (!e.empty()) return "The FSH could not be unpacked: " + e + ".";
        f = unpacked.data(); n = unpacked.size();
    }
    if (n < 16 || memcmp(f, "SHPI", 4)) return "This is not a valid FSH file (missing 'SHPI' signature).";
    uint32_t count = Rd32(f + 8);
    if (count == 0 || count > 4096 || 16 + size_t(count) * 8 > n) return "The FSH directory is corrupted.";

    static const char *kSupported = "Use DXT1/DXT3/DXT5, 32-bit (8888), 24-bit (888), 16-bit (565/1555/4444) or 8-bit palettized.";
    for (uint32_t e = 0; e < count; e++) {
        uint32_t off = Rd32(f + 16 + e * 8 + 4);
        if (off < 16 || off + 16 > n) return "The FSH directory points outside the file.";
        const uint8_t *hdr = f + off;
        uint8_t code = hdr[0];
        if (code & 0x80) return "Compressed FSH entries are not supported. Save the FSH uncompressed.";
        uint32_t w = Rd16(hdr + 4), h = Rd16(hdr + 6);
        if (w == 0 || h == 0) continue;
        if (w > kMaxDim || h > kMaxDim)
            return "The FSH image is " + std::to_string(w) + "x" + std::to_string(h) + "; the maximum is " +
                   std::to_string(kMaxDim) + "x" + std::to_string(kMaxDim) + ".";
        uint32_t blockSize = Rd24(hdr + 1);
        const uint8_t *px = hdr + 16;
        size_t avail = (blockSize && off + size_t(blockSize) <= n) ? blockSize - 16 : n - off - 16;

        out = Image();
        out.width = w; out.height = h;
        bool dxt = code == 0x60 || code == 0x61 || code == 0x62;
        if (dxt) {
            size_t bw = (w + 3) / 4, bh = (h + 3) / 4, bb = code == 0x60 ? 8 : 16;
            if (avail < bw * bh * bb) return "The FSH image data is truncated.";
            if (code == 0x60 && w % 4 == 0 && h % 4 == 0) {
                out.isDxt1 = true;
                out.formatName = "FSH DXT1";
                out.dxt1.assign(px, px + bw * bh * 8);
                return std::string();
            }
            out.formatName = code == 0x60 ? "FSH DXT1" : code == 0x61 ? "FSH DXT3" : "FSH DXT5";
            out.rgba.assign(size_t(w) * h * 4, 0);
            for (size_t by = 0; by < bh; by++)
                for (size_t bx = 0; bx < bw; bx++) {
                    uint8_t b[64];
                    const uint8_t *blk = px + (by * bw + bx) * bb;
                    if (code == 0x60) { rgbcx::unpack_bc1(blk, b); }
                    else if (code == 0x61) DecodeDxt3Block(blk, b);
                    else DecodeDxt5Block(blk, b);
                    for (size_t y = 0; y < 4; y++)
                        for (size_t x = 0; x < 4; x++) {
                            size_t sx = bx * 4 + x, sy = by * 4 + y;
                            if (sx < w && sy < h) memcpy(&out.rgba[(sy * w + sx) * 4], b + (y * 4 + x) * 4, 4);
                        }
                }
            return std::string();
        }

        size_t pixels = size_t(w) * h;
        out.rgba.resize(pixels * 4);
        uint8_t *d = out.rgba.data();
        auto need = [&](size_t bytes) { return avail >= bytes; };
        switch (code) {
        case 0x7D: // B,G,R,A
            if (!need(pixels * 4)) return "The FSH image data is truncated.";
            for (size_t i = 0; i < pixels; i++) { d[i*4] = px[i*4+2]; d[i*4+1] = px[i*4+1]; d[i*4+2] = px[i*4]; d[i*4+3] = px[i*4+3]; }
            out.formatName = "FSH 32-bit 8888"; return std::string();
        case 0x7F: // B,G,R
            if (!need(pixels * 3)) return "The FSH image data is truncated.";
            for (size_t i = 0; i < pixels; i++) { d[i*4] = px[i*3+2]; d[i*4+1] = px[i*3+1]; d[i*4+2] = px[i*3]; d[i*4+3] = 255; }
            out.formatName = "FSH 24-bit 888"; return std::string();
        case 0x78: case 0x7E: case 0x6D: {
            if (!need(pixels * 2)) return "The FSH image data is truncated.";
            for (size_t i = 0; i < pixels; i++) {
                uint16_t v = Rd16(px + i * 2);
                if (code == 0x78) { d[i*4] = uint8_t(((v >> 11) & 31) * 255 / 31); d[i*4+1] = uint8_t(((v >> 5) & 63) * 255 / 63); d[i*4+2] = uint8_t((v & 31) * 255 / 31); d[i*4+3] = 255; }
                else if (code == 0x7E) { d[i*4] = uint8_t(((v >> 10) & 31) * 255 / 31); d[i*4+1] = uint8_t(((v >> 5) & 31) * 255 / 31); d[i*4+2] = uint8_t((v & 31) * 255 / 31); d[i*4+3] = (v & 0x8000) ? 255 : 0; }
                else { d[i*4] = uint8_t(((v >> 8) & 15) * 17); d[i*4+1] = uint8_t(((v >> 4) & 15) * 17); d[i*4+2] = uint8_t((v & 15) * 17); d[i*4+3] = uint8_t((v >> 12) * 17); }
            }
            out.formatName = code == 0x78 ? "FSH 16-bit 565" : code == 0x7E ? "FSH 16-bit 1555" : "FSH 16-bit 4444";
            return std::string();
        }
        case 0x7B: { // 8-bit indices + palette attachment
            if (!need(pixels)) return "The FSH image data is truncated.";
            const uint8_t *pal = nullptr; uint8_t palCode = 0; uint32_t palCount = 0;
            size_t a = off;
            for (uint32_t next = blockSize; next && a + next + 16 <= n; ) {
                a += next;
                const uint8_t *ah = f + a;
                uint8_t c = ah[0];
                if (c == 0x2A || c == 0x24 || c == 0x2D || c == 0x29) { pal = ah + 16; palCode = c; palCount = Rd16(ah + 4); break; }
                next = Rd24(ah + 1);
            }
            if (!pal) return "The 8-bit FSH image has no palette.";
            size_t palBytes = palCode == 0x2A ? 4 : palCode == 0x24 ? 3 : 2;
            if (palCount == 0 || palCount > 256 || (pal - f) + palCount * palBytes > n) return "The FSH palette is corrupted.";
            for (size_t i = 0; i < pixels; i++) {
                uint32_t k = px[i];
                if (k >= palCount) k = 0;
                const uint8_t *c = pal + k * palBytes;
                if (palCode == 0x2A) { d[i*4] = c[2]; d[i*4+1] = c[1]; d[i*4+2] = c[0]; d[i*4+3] = c[3]; }
                else if (palCode == 0x24) { d[i*4] = c[2]; d[i*4+1] = c[1]; d[i*4+2] = c[0]; d[i*4+3] = 255; }
                else {
                    uint16_t v = Rd16(c);
                    if (palCode == 0x2D) { d[i*4] = uint8_t(((v >> 10) & 31) * 255 / 31); d[i*4+1] = uint8_t(((v >> 5) & 31) * 255 / 31); d[i*4+2] = uint8_t((v & 31) * 255 / 31); d[i*4+3] = (v & 0x8000) ? 255 : 0; }
                    else { d[i*4] = uint8_t(((v >> 11) & 31) * 255 / 31); d[i*4+1] = uint8_t(((v >> 5) & 63) * 255 / 63); d[i*4+2] = uint8_t((v & 31) * 255 / 31); d[i*4+3] = 255; }
                }
            }
            out.formatName = "FSH 8-bit palettized"; return std::string();
        }
        default:
            if (code >= 0x60 && code <= 0x7F) {
                char buf[64]; snprintf(buf, sizeof buf, "Unsupported FSH image format 0x%02X. ", code);
                return buf + std::string(kSupported);
            }
            continue; // not a bitmap entry (e.g. palette/attachment); keep looking
        }
    }
    return "The FSH file contains no image.";
}

// ---- PNG / JPG ----
inline std::string ParseImage(const uint8_t *f, size_t n, FileKind kind, Image &out) {
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(f, int(n), &w, &h, &comp))
        return std::string("The ") + KindName(kind) + " could not be read (" + stbi_failure_reason() + ").";
    if (w <= 0 || h <= 0) return "The image has zero width or height.";
    if (uint32_t(w) > kMaxDim || uint32_t(h) > kMaxDim)
        return "The image is " + std::to_string(w) + "x" + std::to_string(h) + "; the maximum is " +
               std::to_string(kMaxDim) + "x" + std::to_string(kMaxDim) + ".";
    unsigned char *px = stbi_load_from_memory(f, int(n), &w, &h, &comp, 4);
    if (!px) return std::string("The ") + KindName(kind) + " could not be decoded (" + stbi_failure_reason() + ").";
    out = Image();
    out.width = uint32_t(w); out.height = uint32_t(h);
    out.rgba.assign(px, px + size_t(w) * h * 4);
    stbi_image_free(px);
    out.formatName = std::string(KindName(kind)) + (comp == 4 || comp == 2 ? " with alpha" : "");
    return std::string();
}

// Decode any supported input, chosen by content. 'kind' receives what was detected.
inline std::string ParseAny(const uint8_t *f, size_t n, Image &out, FileKind &kind) {
    kind = Sniff(f, n);
    switch (kind) {
    case FileKind::Dds: return ParseDDS(f, n, out);
    case FileKind::Fsh: return ParseFSH(f, n, out);
    case FileKind::Png:
    case FileKind::Jpg: return ParseImage(f, n, kind, out);
    default: return "Unrecognized file format. Use FSH, DDS, PNG or JPG.";
    }
}

} // namespace splash
