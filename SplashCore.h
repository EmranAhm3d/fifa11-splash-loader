// SplashCore.h - platform-independent core of SplashLoader
//  * DDS parsing (DXT1/3/5, 24/32-bit RGB(A), DX10 header variants)
//  * Conversion to DXT1 - the ONLY format FIFA 11's boot texture bank displays correctly
//    (fifa.exe 0xC89D40 knows only FSH 0x60/0x61/0x62, and the copy loop at 0xC8A5E1 copies
//     2*width bytes per block row, which is right for DXT1 only)
//  * Building an in-memory splash.fsh (SHPI) identical in layout to the one in fifa.exe
//  * Signature-free locator for the "splash.fsh" in-memory file reference inside fifa.exe
//
// No Windows dependencies so it can be unit-tested natively.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include "vendor/rgbcx.h"   // define RGBCX_IMPLEMENTATION in exactly one .cpp

namespace splash {

const uint8_t FSH_DXT1 = 0x60;
const uint32_t kMaxDim = 4096;

// Result of reading splash.dds: either DXT1 blocks to pass straight through, or RGBA8 pixels to encode.
struct Image {
    uint32_t width = 0, height = 0;   // as stored in the DDS
    std::string formatName;
    bool isDxt1 = false;
    std::vector<uint8_t> dxt1;        // when isDxt1
    std::vector<uint8_t> rgba;        // otherwise: width*height*4, R,G,B,A
};

// What goes into the FSH.
struct Dxt1Texture {
    uint32_t width = 0, height = 0;   // multiples of 4
    std::vector<uint8_t> blocks;
};

inline uint32_t Rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
inline uint16_t Rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
inline void Wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
inline void Wr16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

inline uint32_t FourCC(char a, char b, char c, char d) {
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16) | (uint32_t(uint8_t(d)) << 24);
}

inline bool MaskInfo(uint32_t mask, uint32_t &shift, uint32_t &bits) {
    if (!mask) { shift = 0; bits = 0; return true; }
    shift = 0; while (!(mask & 1u)) { mask >>= 1; shift++; }
    bits = 0; while (mask & 1u) { mask >>= 1; bits++; }
    return mask == 0; // contiguous
}

// Decode a DXT color block in 4-color mode (as BC2/BC3 hardware always does). out: 16 RGBA pixels.
inline void DecodeColorBlock4(const uint8_t *b, uint8_t *out) {
    uint16_t c0 = Rd16(b), c1 = Rd16(b + 2);
    uint32_t idx = Rd32(b + 4);
    int pal[4][3];
    auto expand = [](uint16_t c, int *rgb) {
        rgb[0] = ((c >> 11) & 31) * 255 / 31; rgb[1] = ((c >> 5) & 63) * 255 / 63; rgb[2] = (c & 31) * 255 / 31;
    };
    expand(c0, pal[0]); expand(c1, pal[1]);
    for (int k = 0; k < 3; k++) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
    for (int i = 0; i < 16; i++) {
        int s = (idx >> (2 * i)) & 3;
        out[i * 4 + 0] = uint8_t(pal[s][0]); out[i * 4 + 1] = uint8_t(pal[s][1]); out[i * 4 + 2] = uint8_t(pal[s][2]);
    }
}

inline void DecodeDxt3Block(const uint8_t *b, uint8_t *out) {
    DecodeColorBlock4(b + 8, out);
    for (int i = 0; i < 16; i++) { int a = (b[i / 2] >> ((i & 1) * 4)) & 15; out[i * 4 + 3] = uint8_t(a * 17); }
}

inline void DecodeDxt5Block(const uint8_t *b, uint8_t *out) {
    DecodeColorBlock4(b + 8, out);
    int a0 = b[0], a1 = b[1], pal[8];
    pal[0] = a0; pal[1] = a1;
    if (a0 > a1) for (int i = 1; i < 7; i++) pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    else { for (int i = 1; i < 5; i++) pal[i + 1] = ((5 - i) * a0 + i * a1) / 5; pal[6] = 0; pal[7] = 255; }
    uint64_t bits = 0; for (int i = 0; i < 6; i++) bits |= uint64_t(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; i++) out[i * 4 + 3] = uint8_t(pal[(bits >> (3 * i)) & 7]);
}

// Returns an empty string on success, otherwise a human-readable error.
inline std::string ParseDDS(const uint8_t *f, size_t fileSize, Image &out) {
    if (fileSize < 128)
        return "The file is too small to be a DDS (" + std::to_string(fileSize) + " bytes).";
    if (memcmp(f, "DDS ", 4) != 0)
        return "This is not a DDS file (missing 'DDS ' signature).";
    const uint8_t *h = f + 4;
    if (Rd32(h + 0) != 124)
        return "The DDS header is corrupted (header size is not 124).";
    uint32_t height = Rd32(h + 8);
    uint32_t width  = Rd32(h + 12);
    uint32_t pfFlags   = Rd32(h + 76);
    uint32_t fourCC    = Rd32(h + 80);
    uint32_t bitCount  = Rd32(h + 84);
    uint32_t rMask = Rd32(h + 88), gMask = Rd32(h + 92), bMask = Rd32(h + 96), aMask = Rd32(h + 100);
    uint32_t caps2 = Rd32(h + 108);

    if (caps2 & 0x200)    return "Cube map DDS files are not supported. Save it as a normal 2D texture.";
    if (caps2 & 0x200000) return "Volume DDS files are not supported. Save it as a normal 2D texture.";
    if (width == 0 || height == 0) return "The DDS has zero width or height.";
    if (width > kMaxDim || height > kMaxDim)
        return "The DDS is " + std::to_string(width) + "x" + std::to_string(height) + "; the maximum is " +
               std::to_string(kMaxDim) + "x" + std::to_string(kMaxDim) + ".";

    size_t dataOffset = 128;
    enum { K_NONE, K_DXT1, K_DXT3, K_DXT5, K_BGRA, K_BGRX, K_RGBA, K_RGBX, K_MASKED } kind = K_NONE;
    const uint32_t DDPF_ALPHAPIXELS = 0x1, DDPF_FOURCC = 0x4, DDPF_RGB = 0x40;

    if (pfFlags & DDPF_FOURCC) {
        if (fourCC == FourCC('D','X','T','1')) kind = K_DXT1;
        else if (fourCC == FourCC('D','X','T','3')) kind = K_DXT3;
        else if (fourCC == FourCC('D','X','T','5')) kind = K_DXT5;
        else if (fourCC == 21) kind = K_BGRA;  // D3DFMT_A8R8G8B8
        else if (fourCC == 22) kind = K_BGRX;  // D3DFMT_X8R8G8B8
        else if (fourCC == 32) kind = K_RGBA;  // D3DFMT_A8B8G8R8
        else if (fourCC == 33) kind = K_RGBX;  // D3DFMT_X8B8G8R8
        else if (fourCC == FourCC('D','X','1','0')) {
            if (fileSize < 148) return "The DDS DX10 header is truncated.";
            const uint8_t *dx10 = f + 128;
            uint32_t dxgi = Rd32(dx10 + 0), dim = Rd32(dx10 + 4), misc = Rd32(dx10 + 8), arraySize = Rd32(dx10 + 12);
            if (dim != 3) return "Only 2D DDS textures are supported (DX10 resource dimension is not TEXTURE2D).";
            if (misc & 0x4) return "Cube map DDS files are not supported. Save it as a normal 2D texture.";
            if (arraySize > 1) return "Texture array DDS files are not supported.";
            dataOffset = 148;
            switch (dxgi) {
            case 70: case 71: case 72: kind = K_DXT1; break; // BC1
            case 73: case 74: case 75: kind = K_DXT3; break; // BC2
            case 76: case 77: case 78: kind = K_DXT5; break; // BC3
            case 87: case 91: kind = K_BGRA; break;          // B8G8R8A8 (UNORM/SRGB)
            case 88: case 93: kind = K_BGRX; break;          // B8G8R8X8 (UNORM/SRGB)
            case 28: case 29: kind = K_RGBA; break;          // R8G8B8A8 (UNORM/SRGB)
            default:
                return "Unsupported DX10 DDS format (DXGI format " + std::to_string(dxgi) +
                       "). Save as 32-bit uncompressed A8R8G8B8 (best) or DXT1.";
            }
        }
        else if (fourCC == FourCC('D','X','T','2') || fourCC == FourCC('D','X','T','4'))
            return "Premultiplied-alpha DXT2/DXT4 is not supported. Save as 32-bit uncompressed A8R8G8B8 (best) or DXT1.";
        else {
            char cc[5] = { char(fourCC & 0xFF), char((fourCC >> 8) & 0xFF), char((fourCC >> 16) & 0xFF), char(fourCC >> 24), 0 };
            for (int i = 0; i < 4; i++) if (cc[i] < 32 || cc[i] > 126) cc[i] = '?';
            return std::string("Unsupported DDS compression '") + cc + "'. Save as 32-bit uncompressed A8R8G8B8 (best) or DXT1.";
        }
    }
    else if (pfFlags & DDPF_RGB) {
        if (bitCount != 32 && bitCount != 24)
            return "Unsupported " + std::to_string(bitCount) + "-bit uncompressed DDS. Save as 32-bit A8R8G8B8 (best) or DXT1.";
        kind = K_MASKED;
        if (!(pfFlags & DDPF_ALPHAPIXELS)) aMask = 0;
    }
    else
        return "Unsupported DDS pixel format (not RGB and not DXT). Save as 32-bit A8R8G8B8 (best) or DXT1.";

    out = Image();
    out.width = width;
    out.height = height;
    const uint8_t *src = f + dataOffset;
    size_t avail = fileSize - dataOffset;

    if (kind == K_DXT1 || kind == K_DXT3 || kind == K_DXT5) {
        if ((width % 4) != 0 || (height % 4) != 0)
            return "DXT-compressed splash must have width and height divisible by 4 (this one is " +
                   std::to_string(width) + "x" + std::to_string(height) + ").";
        size_t blockBytes = (kind == K_DXT1) ? 8 : 16;
        size_t bw = width / 4, bh = height / 4;
        size_t size = bw * bh * blockBytes;
        if (avail < size)
            return "The DDS file is truncated (needs " + std::to_string(size) + " bytes of pixel data, has " + std::to_string(avail) + ").";
        if (kind == K_DXT1) {
            out.isDxt1 = true;
            out.formatName = "DXT1";
            out.dxt1.assign(src, src + size);
            return std::string();
        }
        out.formatName = (kind == K_DXT3) ? "DXT3" : "DXT5";
        out.rgba.resize(size_t(width) * height * 4);
        for (size_t by = 0; by < bh; by++)
            for (size_t bx = 0; bx < bw; bx++) {
                uint8_t px[64];
                const uint8_t *blk = src + (by * bw + bx) * 16;
                if (kind == K_DXT3) DecodeDxt3Block(blk, px); else DecodeDxt5Block(blk, px);
                for (int y = 0; y < 4; y++)
                    memcpy(&out.rgba[((by * 4 + y) * width + bx * 4) * 4], px + y * 16, 16);
            }
        return std::string();
    }

    uint32_t bpp = (kind == K_MASKED) ? bitCount / 8 : 4;
    size_t size = size_t(width) * bpp * height;
    if (avail < size)
        return "The DDS file is truncated (needs " + std::to_string(size) + " bytes of pixel data, has " + std::to_string(avail) + ").";

    if (kind == K_BGRA || kind == K_BGRX) { rMask = 0x00FF0000; gMask = 0x0000FF00; bMask = 0x000000FF; aMask = (kind == K_BGRA) ? 0xFF000000 : 0; }
    if (kind == K_RGBA || kind == K_RGBX) { rMask = 0x000000FF; gMask = 0x0000FF00; bMask = 0x00FF0000; aMask = (kind == K_RGBA) ? 0xFF000000 : 0; }

    uint32_t rs, rb, gs, gb, bs, bb, as, ab;
    if (!MaskInfo(rMask, rs, rb) || !MaskInfo(gMask, gs, gb) || !MaskInfo(bMask, bs, bb) || !MaskInfo(aMask, as, ab) ||
        rb != 8 || gb != 8 || bb != 8 || (ab != 0 && ab != 8))
        return "Unsupported uncompressed DDS channel layout. Save as 32-bit A8R8G8B8 (best) or DXT1.";

    out.formatName = std::to_string(bpp * 8) + "-bit uncompressed" + (ab ? " with alpha" : "");
    out.rgba.resize(size_t(width) * height * 4);
    uint8_t *dst = out.rgba.data();
    for (size_t i = 0, n = size_t(width) * height; i < n; i++) {
        const uint8_t *p = src + i * bpp;
        uint32_t px = p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (bpp == 4 ? (uint32_t(p[3]) << 24) : 0);
        dst[i * 4 + 0] = uint8_t(px >> rs);
        dst[i * 4 + 1] = uint8_t(px >> gs);
        dst[i * 4 + 2] = uint8_t(px >> bs);
        dst[i * 4 + 3] = ab ? uint8_t(px >> as) : 0xFF;
    }
    return std::string();
}

// Encode RGBA8 to DXT1 with rgbcx (call rgbcx::init() once beforehand).
// Pixels with alpha < 255 are composited over black (the splash is opaque in game).
// Sizes that are not multiples of 4 are padded by repeating the edge pixels.
inline void EncodeDxt1(const Image &img, uint32_t level, Dxt1Texture &out, bool *hadAlpha = nullptr, bool *padded = nullptr) {
    uint32_t w = img.width, h = img.height;
    out.width = (w + 3) & ~3u;
    out.height = (h + 3) & ~3u;
    if (padded) *padded = (out.width != w || out.height != h);
    bool alpha = false;
    uint32_t bw = out.width / 4, bh = out.height / 4;
    out.blocks.resize(size_t(bw) * bh * 8);
    for (uint32_t by = 0; by < bh; by++)
        for (uint32_t bx = 0; bx < bw; bx++) {
            uint8_t px[64];
            for (uint32_t y = 0; y < 4; y++)
                for (uint32_t x = 0; x < 4; x++) {
                    uint32_t sx = bx * 4 + x, sy = by * 4 + y;
                    if (sx >= w) sx = w - 1;
                    if (sy >= h) sy = h - 1;
                    const uint8_t *s = &img.rgba[(size_t(sy) * w + sx) * 4];
                    uint8_t *d = px + (y * 4 + x) * 4;
                    if (s[3] != 255) {
                        alpha = true;
                        d[0] = uint8_t((s[0] * s[3] + 127) / 255); d[1] = uint8_t((s[1] * s[3] + 127) / 255); d[2] = uint8_t((s[2] * s[3] + 127) / 255);
                    } else { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; }
                    d[3] = 255;
                }
            // allow 3-color blocks (indices 0..2 only), never the transparent-black index
            rgbcx::encode_bc1(level, &out.blocks[(size_t(by) * bw + bx) * 8], px, true, false);
        }
    if (hadAlpha) *hadAlpha = alpha;
}

// Builds splash.fsh with exactly the same layout as the original embedded in fifa.exe:
//   SHPI header, dir "G359", one DXT1 entry "fifa", "Buy ERTS" pad, pixel data,
//   then the 0x70 name attachment "fifasplash" and the 0x6F attachment copied verbatim.
inline std::string BuildFSH(const Dxt1Texture &tex, std::vector<uint8_t> &fsh) {
    const size_t L = tex.blocks.size();
    const size_t blockSize = ((0x10 + L + 15) & ~size_t(15)) + 0x10; // header -> first attachment
    if (blockSize > 0xFFFFFF)
        return "The splash is too large for an FSH entry (" + std::to_string(tex.width) + "x" + std::to_string(tex.height) + ").";
    const size_t attachOff = 0x20 + blockSize;
    const size_t total = attachOff + 0x20;
    fsh.assign(total, 0);
    uint8_t *p = fsh.data();
    memcpy(p + 0x00, "SHPI", 4);
    Wr32(p + 0x04, uint32_t(total));
    Wr32(p + 0x08, 1);
    memcpy(p + 0x0C, "G359", 4);
    memcpy(p + 0x10, "fifa", 4);
    Wr32(p + 0x14, 0x20);
    memcpy(p + 0x18, "Buy ERTS", 8);
    Wr32(p + 0x20, uint32_t(FSH_DXT1) | (uint32_t(blockSize) << 8));
    Wr16(p + 0x24, uint16_t(tex.width));
    Wr16(p + 0x26, uint16_t(tex.height));
    // centerX, centerY, posX, posY(+mip nibble) must be 0: fifa.exe asserts posX/posY == 0
    memcpy(p + 0x30, tex.blocks.data(), L);
    static const uint8_t attachments[0x20] = {
        0x70, 0x10, 0x00, 0x00, 'f', 'i', 'f', 'a', 's', 'p', 'l', 'a', 's', 'h', 0x00, 0x00,
        0x6F, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xA8, 0xB2, 0x82, 0x02, 0x00, 0x00, 0x00, 0x00
    };
    memcpy(p + attachOff, attachments, sizeof(attachments));
    return std::string();
}

// ---------------------------------------------------------------------------------------------
// Locator
//
// In fifa.exe the splash is registered as an in-memory file:
//      mov  eax, [blobSize]              ; size dword sits right before the blob
//      ...
//      push offset "splash.fsh"
//      ...
//      mov  [esp+XX], offset blob        ; blob starts with "chunkzip" (or chunkref/chunklzx/SHPI)
//      mov  [esp+YY], eax
//      call LoadResourceFromMemory
// We find the "splash.fsh" string, every code reference to it, and then within a small window
// around each reference the imm32 operand pointing at a compressed/FSH blob and the operand that
// points at the dword right before it. No version-specific bytes are needed.
// ---------------------------------------------------------------------------------------------

struct Range { uint32_t start, end; }; // [start, end) virtual addresses

struct ImageView {
    const uint8_t *mem = nullptr; // byte for VA v is mem[v - base]
    uint32_t base = 0;
    std::vector<Range> readable;  // all readable ranges
    std::vector<Range> code;      // executable ranges (subset of readable)

    bool Readable(uint32_t va, uint32_t len) const {
        for (auto const &r : readable)
            if (va >= r.start && va <= r.end && len <= r.end - va) return true;
        return false;
    }
    const uint8_t *At(uint32_t va) const { return mem + (va - base); }
    uint32_t Dword(uint32_t va) const { return Rd32(At(va)); }
};

struct Site {
    uint32_t stringVA = 0;   // "splash.fsh"
    uint32_t refVA = 0;      // operand that references the string
    uint32_t ptrOperand = 0; // imm32 holding the blob pointer
    uint32_t sizeOperand = 0;// disp32 holding the address of the size dword
    uint32_t blobVA = 0;
    uint32_t blobSize = 0;
    char blobMagic[9] = {};
};

inline bool IsSplashBlob(const ImageView &v, uint32_t va) {
    if (!v.Readable(va - 4, 12)) return false;
    const uint8_t *p = v.At(va);
    bool magic = !memcmp(p, "chunkzip", 8) || !memcmp(p, "chunkref", 8) || !memcmp(p, "chunklzx", 8) || !memcmp(p, "SHPI", 4);
    if (!magic) return false;
    uint32_t size = v.Dword(va - 4);
    return size >= 16 && size <= 64u * 1024u * 1024u;
}

inline std::vector<Site> FindSplashSites(const ImageView &v, std::string *diag = nullptr) {
    static const char kName[] = "splash.fsh";
    const uint32_t nameLen = sizeof(kName); // includes terminating NUL
    std::vector<Site> sites;
    unsigned strings = 0, refs = 0;

    for (auto const &r : v.readable) {
        if (r.end - r.start < nameLen) continue;
        for (uint32_t s = r.start; s + nameLen <= r.end; s++) {
            const uint8_t *p = v.At(s);
            if (p[0] != 's' || memcmp(p, kName, nameLen) != 0) continue;
            strings++;
            uint8_t pat[4]; Wr32(pat, s);
            for (auto const &c : v.code) {
                if (c.end - c.start < 4) continue;
                for (uint32_t a = c.start; a + 4 <= c.end; a++) {
                    const uint8_t *q = v.At(a);
                    if (q[0] != pat[0] || memcmp(q, pat, 4) != 0) continue;
                    refs++;
                    const uint32_t win = 0x60;
                    uint32_t lo = (a - c.start > win) ? a - win : c.start;
                    uint32_t hi = (c.end - a > win + 4) ? a + win : c.end - 4;
                    for (uint32_t i = lo; i <= hi; i++) {
                        uint32_t blob = v.Dword(i);
                        if (!IsSplashBlob(v, blob)) continue;
                        uint32_t sizeAddr = blob - 4;
                        for (uint32_t k = lo; k <= hi; k++) {
                            if (k + 4 > i && k < i + 4) continue; // not overlapping the pointer operand
                            if (v.Dword(k) != sizeAddr) continue;
                            Site st;
                            st.stringVA = s; st.refVA = a; st.ptrOperand = i; st.sizeOperand = k;
                            st.blobVA = blob; st.blobSize = v.Dword(sizeAddr);
                            memcpy(st.blobMagic, v.At(blob), 8);
                            for (int m = 0; m < 8; m++) if (st.blobMagic[m] < 32 || st.blobMagic[m] > 126) st.blobMagic[m] = '.';
                            bool dup = false;
                            for (auto const &e : sites) if (e.ptrOperand == st.ptrOperand) dup = true;
                            if (!dup) sites.push_back(st);
                        }
                    }
                }
            }
        }
    }
    if (diag)
        *diag = "string hits: " + std::to_string(strings) + ", code refs: " + std::to_string(refs) + ", sites: " + std::to_string(sites.size());
    return sites;
}

} // namespace splash
