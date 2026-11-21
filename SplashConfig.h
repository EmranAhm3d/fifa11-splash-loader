// SplashConfig.h - which splash file to use, from SplashLoader.ini (pure logic, unit-testable).
//
//   [Splash]
//   Enabled=1        0 = do nothing (original splash)
//   Format=auto      auto = first found of fsh > dds > png > jpg;  or force: fsh / dds / png / jpg
//   File=            optional explicit file (relative to the Game folder, or absolute); overrides Format
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace splash {

struct Config {
    bool enabled = true;
    std::string format = "auto";
    std::string file;
};

struct Selection {
    std::string path;                 // chosen file (relative to the Game folder unless absolute); empty = vanilla
    std::string reason;               // for the log
    std::vector<std::string> ignored; // other candidates that exist but were not used
    std::string warning;              // non-fatal: show once, carry on
    std::string error;                // fatal: show, use the original splash
};

inline std::string TrimLower(std::string s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) i++;
    s.erase(0, i);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(tolower(c)); });
    return s;
}

inline std::string Trim(std::string s) {
    while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) i++;
    s.erase(0, i);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

// exists(path) -> bool, path relative to the Game folder (or absolute)
template <typename ExistsFn>
Selection SelectSource(Config const &cfg, ExistsFn exists) {
    Selection sel;
    const std::string dir = "data\\fifarna\\";
    if (!cfg.enabled) {
        sel.reason = "Enabled=0 in SplashLoader.ini - the original splash will be used.";
        return sel;
    }
    std::string file = Trim(cfg.file);
    if (!file.empty()) {
        if (exists(file)) { sel.path = file; sel.reason = "File= in SplashLoader.ini"; }
        else sel.error = "SplashLoader.ini sets File=" + file + " but that file was not found.";
        return sel;
    }

    struct Fmt { const char *name; std::vector<const char *> exts; };
    static const std::vector<Fmt> ladder = {
        { "fsh", { "fsh" } }, { "dds", { "dds" } }, { "png", { "png" } }, { "jpg", { "jpg", "jpeg" } }
    };
    std::string format = TrimLower(cfg.format);
    if (format.empty()) format = "auto";
    if (format == "jpeg") format = "jpg";
    bool known = format == "auto";
    for (auto const &f : ladder) if (format == f.name) known = true;
    if (!known) {
        sel.warning = "SplashLoader.ini has an unknown Format=" + cfg.format + " (use auto, fsh, dds, png or jpg). Using auto.";
        format = "auto";
    }

    if (format == "auto") {
        for (auto const &f : ladder)
            for (auto ext : f.exts) {
                std::string p = dir + "splash." + ext;
                if (!exists(p)) continue;
                if (sel.path.empty()) { sel.path = p; sel.reason = "Format=auto (priority fsh > dds > png > jpg)"; }
                else sel.ignored.push_back(p);
            }
        if (sel.path.empty()) sel.reason = "No splash.fsh/.dds/.png/.jpg found - the original splash will be used.";
        return sel;
    }

    for (auto const &f : ladder) {
        if (format != f.name) continue;
        for (auto ext : f.exts) {
            std::string p = dir + "splash." + ext;
            if (exists(p)) { sel.path = p; sel.reason = "Format=" + format + " in SplashLoader.ini"; return sel; }
        }
        sel.error = "SplashLoader.ini sets Format=" + format + " but " + dir + "splash." + f.exts[0] + " was not found.";
    }
    return sel;
}

} // namespace splash
