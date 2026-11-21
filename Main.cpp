// SplashLoader.asi v1.0 - custom boot splash for FIFA 11 (PC)
//
// Put your image in <game>\data\fifarna\ as splash.fsh, splash.dds, splash.png or splash.jpg.
// plugins\SplashLoader.ini (optional) picks which one; without it the first found of
// fsh > dds > png > jpg is used. No file = the original splash.
//
// How it works:
//   fifa.exe carries splash.fsh as a chunkzip blob in its .data section and hands it to the
//   resource system as an in-memory file ("splash.fsh", pointer, size). This plugin builds a
//   DXT1 FSH in memory and redirects that pointer/size to it.
//   The boot texture bank can only display DXT1 (see SplashCore.h), so every other input is
//   converted to DXT1 with the rgbcx encoder at high quality. The result is cached in
//   plugins\SplashLoader.cache and only re-encoded when the source file changes.
//   The splash quad is drawn at 16:9, so 1280x720 or 1920x1080 is a native fit.

#define RGBCX_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#include "plugin.h"
#include "SplashCore.h"
#include "SplashInputs.h"
#include "SplashConfig.h"
#include <cstdarg>
#include <cstdio>

using namespace plugin;

namespace {

uint8_t *gFsh = nullptr;     // never freed: the game keeps reading it
uint32_t gFshSize = 0;       // the game reads the size from here after patching
FILE *gLog = nullptr;

HMODULE ThisModule() {
    HMODULE h = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&ThisModule), &h);
    return h;
}

std::wstring ModuleSiblingPath(wchar_t const *ext) {
    std::wstring path = FIFA::GetModulePath(ThisModule());
    auto dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos) path.resize(dot);
    return path + ext;
}

void Log(const char *fmt, ...) {
    if (!gLog) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(gLog, fmt, ap);
    va_end(ap);
    fputc('\n', gLog);
    fflush(gLog);
}

void Fail(std::string const &msg) {
    Log("ERROR: %s", msg.c_str());
    Log("The original splash will be used.");
    Warning("%s\n\nThe original FIFA 11 splash will be shown instead.", msg.c_str());
}

bool ReadWholeFile(std::wstring const &path, std::vector<uint8_t> &data) {
    FILE *f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return false; }
    data.resize(size_t(size));
    size_t got = size ? fread(data.data(), 1, data.size(), f) : 0;
    fclose(f);
    return got == data.size();
}

bool IsAbsolute(std::string const &p) {
    return (p.size() >= 2 && p[1] == ':') || (p.size() >= 2 && (p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/'));
}

std::wstring ResolvePath(std::string const &p) {
    std::wstring w = ToUTF16(p);
    return IsAbsolute(p) ? w : FIFA::GameDirPath(w);
}

// ---- config ----
splash::Config ReadConfig(bool &found) {
    splash::Config cfg;
    std::wstring ini = ModuleSiblingPath(L".ini");
    found = FileExistsW(ini);
    if (!found) return cfg;
    wchar_t buf[1024];
    cfg.enabled = GetPrivateProfileIntW(L"Splash", L"Enabled", 1, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"Splash", L"Format", L"auto", buf, 1024, ini.c_str());
    cfg.format = ToUTF8(buf);
    GetPrivateProfileStringW(L"Splash", L"File", L"", buf, 1024, ini.c_str());
    cfg.file = ToUTF8(buf);
    return cfg;
}

// ---- DXT1 cache: avoids re-encoding on every launch ----
const uint32_t kCacheVersion = 2;
#pragma pack(push, 1)
struct CacheHeader {
    char magic[4];          // "SLC1"
    uint32_t version;
    uint32_t level;
    uint64_t srcSize;
    uint64_t srcWriteTime;
    uint64_t srcPathHash;
    uint32_t width, height; // padded DXT1 size
    uint32_t dataSize;
};
#pragma pack(pop)

uint64_t HashPath(std::wstring const &p) {
    uint64_t h = 1469598103934665603ull;
    for (wchar_t c : ToLower(p)) { h ^= uint16_t(c); h *= 1099511628211ull; }
    return h;
}

bool GetFileStamp(std::wstring const &path, uint64_t &size, uint64_t &writeTime) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    size = (uint64_t(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    writeTime = (uint64_t(fad.ftLastWriteTime.dwHighDateTime) << 32) | fad.ftLastWriteTime.dwLowDateTime;
    return true;
}

bool LoadCache(CacheHeader const &key, splash::Dxt1Texture &tex) {
    std::vector<uint8_t> data;
    if (!ReadWholeFile(ModuleSiblingPath(L".cache"), data) || data.size() < sizeof(CacheHeader)) return false;
    CacheHeader h;
    memcpy(&h, data.data(), sizeof(h));
    if (memcmp(h.magic, "SLC1", 4) || h.version != key.version || h.level != key.level || h.srcSize != key.srcSize ||
        h.srcWriteTime != key.srcWriteTime || h.srcPathHash != key.srcPathHash || h.width % 4 || h.height % 4 ||
        h.dataSize != (h.width / 4) * (h.height / 4) * 8 || data.size() != sizeof(h) + h.dataSize)
        return false;
    tex.width = h.width;
    tex.height = h.height;
    tex.blocks.assign(data.begin() + sizeof(h), data.end());
    return true;
}

void SaveCache(CacheHeader h, splash::Dxt1Texture const &tex) {
    h.width = tex.width; h.height = tex.height; h.dataSize = static_cast<uint32_t>(tex.blocks.size());
    std::wstring path = ModuleSiblingPath(L".cache");
    FILE *f = _wfopen(path.c_str(), L"wb");
    if (!f) { Log("Note: could not write the cache file."); return; }
    bool ok = fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(tex.blocks.data(), 1, tex.blocks.size(), f) == tex.blocks.size();
    fclose(f);
    if (!ok) { _wremove(path.c_str()); Log("Note: could not write the cache file."); }
}

bool IsReadableProtect(DWORD p) {
    if (p & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    return (p & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

// Describe the game's own image (fifa.exe) for the scanner: committed + readable parts of each section.
splash::ImageView DescribeGameImage() {
    splash::ImageView view;
#ifdef SPLASH_TEST
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"fifa.exe")); // test host maps fifa.exe as data+code
#else
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
#endif
    view.mem = reinterpret_cast<const uint8_t *>(base);
    view.base = static_cast<uint32_t>(base);
    auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
    auto nt = reinterpret_cast<PIMAGE_NT_HEADERS32>(base + dos->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        uintptr_t start = base + sec->VirtualAddress;
        uintptr_t end = start + (sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData);
        bool exec = (sec->Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE)) != 0;
        uintptr_t p = start;
        while (p < end) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(reinterpret_cast<LPCVOID>(p), &mbi, sizeof(mbi))) break;
            uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
            if (regionEnd > end) regionEnd = end;
            if (mbi.State == MEM_COMMIT && IsReadableProtect(mbi.Protect)) {
                splash::Range r{ static_cast<uint32_t>(p), static_cast<uint32_t>(regionEnd) };
                if (!view.readable.empty() && view.readable.back().end == r.start) view.readable.back().end = r.end;
                else view.readable.push_back(r);
                if (exec) {
                    if (!view.code.empty() && view.code.back().end == r.start) view.code.back().end = r.end;
                    else view.code.push_back(r);
                }
            }
            p = regionEnd;
        }
    }
    return view;
}

void Run() {
    gLog = _wfopen(ModuleSiblingPath(L".log").c_str(), L"wt");
    auto version = FIFA::GetAppVersion();
    Log("SplashLoader v1.0 for FIFA 11");
    Log("Game: %s, entry point %08X", WtoA(version.name()).c_str(), static_cast<unsigned>(FIFA::GetEntryPoint()));

    // ---- which file? ----
    bool iniFound = false;
    splash::Config cfg = ReadConfig(iniFound);
    Log("Config: %s (Enabled=%d, Format=%s, File=%s)", iniFound ? "SplashLoader.ini" : "no SplashLoader.ini, defaults",
        cfg.enabled ? 1 : 0, cfg.format.c_str(), cfg.file.empty() ? "(none)" : cfg.file.c_str());
    splash::Selection sel = splash::SelectSource(cfg, [](std::string const &p) { return FileExistsW(ResolvePath(p)); });
    if (!sel.warning.empty()) {
        Log("WARNING: %s", sel.warning.c_str());
        Warning("%s", sel.warning.c_str());
    }
    if (!sel.error.empty())
        return Fail(sel.error);
    if (sel.path.empty()) {
        Log("%s", sel.reason.c_str());
        return;
    }
    Log("Using %s (%s)", sel.path.c_str(), sel.reason.c_str());
    for (auto const &ig : sel.ignored)
        Log("  also found %s - ignored (lower priority)", ig.c_str());

    // ---- read + decode ----
    std::wstring srcPath = ResolvePath(sel.path);
    std::vector<uint8_t> file;
    if (!ReadWholeFile(srcPath, file))
        return Fail("Could not read " + sel.path + ".");

    splash::Image img;
    splash::FileKind kind;
    std::string err = splash::ParseAny(file.data(), file.size(), img, kind);
    if (!err.empty())
        return Fail(sel.path + ": " + err);
    file.clear();
    file.shrink_to_fit();
    Log("Read: %ux%u, %s", img.width, img.height, img.formatName.c_str());
    if (img.width * 9 != img.height * 16)
        Log("Note: the splash is drawn at 16:9; a %ux%u image will be stretched to fit.", img.width, img.height);

    // ---- to DXT1 ----
    splash::Dxt1Texture tex;
    if (img.isDxt1) {
        tex.width = img.width;
        tex.height = img.height;
        tex.blocks = std::move(img.dxt1);
        Log("Already DXT1 - used as-is.");
    }
    else {
        // ~0.5 s for 1280x720 at level 14; quality is within 0.1 dB of the maximum level
        uint32_t blocks = ((img.width + 3) / 4) * ((img.height + 3) / 4);
        CacheHeader key{};
        memcpy(key.magic, "SLC1", 4);
        key.version = kCacheVersion;
        key.level = blocks <= 600000 ? 14 : 10;
        key.srcPathHash = HashPath(srcPath);
        bool haveStamp = GetFileStamp(srcPath, key.srcSize, key.srcWriteTime);
        if (haveStamp && LoadCache(key, tex))
            Log("Using cached DXT1 encoding (%ux%u).", tex.width, tex.height);
        else {
            DWORD t0 = GetTickCount();
            rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
            bool hadAlpha = false, padded = false;
            splash::EncodeDxt1(img, key.level, tex, &hadAlpha, &padded);
            Log("Encoded to DXT1 (rgbcx level %u) in %lu ms.", key.level, static_cast<unsigned long>(GetTickCount() - t0));
            if (hadAlpha) Log("Note: transparent pixels were blended over black (the splash is drawn opaque).");
            if (padded) Log("Note: size padded to %ux%u (DXT1 needs multiples of 4).", tex.width, tex.height);
            if (haveStamp) SaveCache(key, tex);
        }
    }
    img = splash::Image();

    std::vector<uint8_t> fsh;
    err = splash::BuildFSH(tex, fsh);
    if (!err.empty())
        return Fail(sel.path + ": " + err);

    // ---- find + patch ----
    std::string diag;
    auto view = DescribeGameImage();
    auto sites = splash::FindSplashSites(view, &diag);
    Log("Scan: %s", diag.c_str());
    if (sites.empty())
        return Fail(Format("Could not find where this fifa.exe loads its splash screen.\n"
                           "Game: %s (entry point %08X).\n"
                           "Please report this together with plugins\\SplashLoader.log.",
                           WtoA(version.name()).c_str(), static_cast<unsigned>(FIFA::GetEntryPoint())));

    gFsh = static_cast<uint8_t *>(VirtualAlloc(nullptr, fsh.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!gFsh)
        return Fail("Not enough memory to load the custom splash.");
    memcpy(gFsh, fsh.data(), fsh.size());
    gFshSize = static_cast<uint32_t>(fsh.size());

    for (auto const &s : sites) {
        Log("Patching: \"splash.fsh\" at %08X (ref %08X), blob %08X ('%s', %u bytes), pointer operand %08X, size operand %08X",
            s.stringVA, s.refVA, s.blobVA, s.blobMagic, s.blobSize, s.ptrOperand, s.sizeOperand);
        patch::SetPointer(s.ptrOperand, gFsh);
        patch::SetPointer(s.sizeOperand, &gFshSize);
    }
    Log("Custom splash installed (%ux%u DXT1, %u-byte FSH).", tex.width, tex.height, gFshSize);
}

} // namespace

class SplashLoader {
public:
    SplashLoader() { Run(); }
} gSplashLoader;
