// Native (Linux) test harness for SplashCore.h
//   test_core <fifa.exe> <orig_splash.dds> <orig_splash.fsh> [extra.dds ...]
#define RGBCX_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "../SplashInputs.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>

static std::vector<uint8_t> ReadAll(const char *path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

int main(int argc, char **argv) {
    if (argc < 4) { printf("usage\n"); return 2; }
    int fails = 0;

    // --- map PE like the Windows loader would ---
    auto exe = ReadAll(argv[1]);
    uint32_t peOff = splash::Rd32(&exe[0x3C]);
    const uint8_t *nt = &exe[peOff];
    uint16_t nsec = *(const uint16_t *)(nt + 6);
    uint16_t optSize = *(const uint16_t *)(nt + 20);
    const uint8_t *opt = nt + 24;
    uint32_t imageBase = splash::Rd32(opt + 28);
    uint32_t sizeOfImage = splash::Rd32(opt + 56);
    uint32_t entry = splash::Rd32(opt + 16);
    std::vector<uint8_t> mapped(sizeOfImage, 0);
    splash::ImageView view;
    view.mem = mapped.data();
    view.base = imageBase;
    const uint8_t *sec = opt + optSize;
    for (int i = 0; i < nsec; i++, sec += 40) {
        uint32_t vsize = splash::Rd32(sec + 8), va = splash::Rd32(sec + 12), rawSize = splash::Rd32(sec + 16), rawPtr = splash::Rd32(sec + 20), ch = splash::Rd32(sec + 36);
        memcpy(&mapped[va], &exe[rawPtr], std::min(rawSize, vsize));
        splash::Range r{ imageBase + va, imageBase + va + vsize };
        view.readable.push_back(r);
        if (ch & (0x20000000 | 0x20)) view.code.push_back(r);
    }
    printf("image base %08X, entry %08X\n", imageBase, imageBase + entry);
    std::string diag;
    auto sites = splash::FindSplashSites(view, &diag);
    printf("scan: %s\n", diag.c_str());
    for (auto &s : sites)
        printf("  site: string %08X ref %08X ptrOperand %08X sizeOperand %08X blob %08X size %X magic '%s'\n",
               s.stringVA, s.refVA, s.ptrOperand, s.sizeOperand, s.blobVA, s.blobSize, s.blobMagic);
    if (sites.size() != 1 || sites[0].ptrOperand != 0x8FAA57 || sites[0].sizeOperand != 0x8FAA36) {
        printf("FAIL: expected exactly one site with ptrOperand 008FAA57 / sizeOperand 008FAA36\n"); fails++;
    } else printf("OK: scanner found the expected FLT 1.0.1.0 patch sites\n");

    rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
    auto toTex = [](splash::Image &im, splash::Dxt1Texture &t, double *ms) {
        auto t0 = std::chrono::steady_clock::now();
        if (im.isDxt1) { t.width = im.width; t.height = im.height; t.blocks = im.dxt1; }
        else splash::EncodeDxt1(im, 14, t);
        *ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    };
    // --- round-trip original (DXT1 passthrough must be byte-identical) ---
    {
        auto dds = ReadAll(argv[2]);
        auto fshOrig = ReadAll(argv[3]);
        splash::Image img; splash::Dxt1Texture tex; std::vector<uint8_t> fsh; double ms;
        auto err = splash::ParseDDS(dds.data(), dds.size(), img);
        if (err.empty()) { toTex(img, tex, &ms); err = splash::BuildFSH(tex, fsh); }
        if (!err.empty()) { printf("FAIL: %s\n", err.c_str()); fails++; }
        else if (fsh != fshOrig) { printf("FAIL: rebuilt FSH differs\n"); fails++; }
        else printf("OK: rebuilt FSH is byte-identical to the original (%zu bytes)\n", fsh.size());
    }
    // --- extra DDS files ---
    for (int i = 4; i < argc; i++) {
        auto d = ReadAll(argv[i]);
        splash::Image im; splash::Dxt1Texture tex; std::vector<uint8_t> f; double ms = 0;
        splash::FileKind kind;
        auto e = splash::ParseAny(d.data(), d.size(), im, kind);
        if (e.empty() && !im.isDxt1) {
            char rp[512]; snprintf(rp, sizeof rp, "%s.rgba", argv[i]);
            std::ofstream(rp, std::ios::binary).write((const char *)im.rgba.data(), im.rgba.size());
        }
        if (e.empty()) { toTex(im, tex, &ms); e = splash::BuildFSH(tex, f); }
        if (!e.empty()) { printf("%s: rejected -> %s\n", argv[i], e.c_str()); continue; }
        // PSNR of the DXT1 result vs the decoded source (skip for passthrough)
        double psnr = 0;
        if (!im.isDxt1) {
            double se = 0; uint32_t bw = tex.width / 4;
            for (uint32_t by = 0; by < tex.height / 4; by++) for (uint32_t bx = 0; bx < bw; bx++) {
                uint8_t px[64]; rgbcx::unpack_bc1(&tex.blocks[(by * bw + bx) * 8], px);
                for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
                    uint32_t sx = std::min(bx * 4 + x, im.width - 1), sy = std::min(by * 4 + y, im.height - 1);
                    const uint8_t *s = &im.rgba[(size_t(sy) * im.width + sx) * 4];
                    for (int c = 0; c < 3; c++) { double v = s[c] * s[3] / 255.0, e2 = px[(y * 4 + x) * 4 + c] - v; se += e2 * e2; }
                }
            }
            psnr = 10 * log10(255.0 * 255 / (se / (double(tex.width) * tex.height * 3)));
        }
        bool okFmt = f[0x20] == 0x60 && splash::Rd32(&f[4]) == f.size();
        printf("%s: %ux%u %s -> DXT1 %ux%u, FSH %zu bytes, %.0f ms%s%s\n", argv[i], im.width, im.height, im.formatName.c_str(),
               tex.width, tex.height, f.size(), ms, im.isDxt1 ? " (passthrough)" : "", okFmt ? "" : "  BAD FSH");
        if (psnr > 0) printf("    PSNR vs source: %.2f dB\n", psnr);
        if (!okFmt) fails++;
        char out[512]; snprintf(out, sizeof out, "%s.out.fsh", argv[i]);
        std::ofstream(out, std::ios::binary).write((const char *)f.data(), f.size());
    }
    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
