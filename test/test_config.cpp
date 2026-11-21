#include "../SplashConfig.h"
#include <cstdio>
#include <set>
int fails = 0;
void check(const char *name, splash::Config c, std::set<std::string> files, std::string wantPath, bool wantError, bool wantWarn = false, size_t wantIgnored = 0) {
    auto s = splash::SelectSource(c, [&](std::string const &p) { return files.count(p) > 0; });
    bool ok = s.path == wantPath && (!s.error.empty()) == wantError && (!s.warning.empty()) == wantWarn && s.ignored.size() == wantIgnored;
    printf("%-44s %s  -> path='%s' err='%s' warn='%s' ignored=%zu\n", name, ok ? "OK  " : "FAIL", s.path.c_str(), s.error.c_str(), s.warning.c_str(), s.ignored.size());
    if (!ok) fails++;
}
int main() {
    const std::string D = "data\\fifarna\\";
    std::set<std::string> all = { D + "splash.fsh", D + "splash.dds", D + "splash.png", D + "splash.jpg" };
    splash::Config a; // defaults: enabled, auto
    check("auto, nothing present", a, {}, "", false);
    check("auto, all four present -> fsh", a, all, D + "splash.fsh", false, false, 3);
    check("auto, dds+png+jpg -> dds", a, { D + "splash.dds", D + "splash.png", D + "splash.jpg" }, D + "splash.dds", false, false, 2);
    check("auto, png+jpg -> png", a, { D + "splash.png", D + "splash.jpg" }, D + "splash.png", false, false, 1);
    check("auto, only .jpeg", a, { D + "splash.jpeg" }, D + "splash.jpeg", false);
    splash::Config p; p.format = " PNG ";
    check("Format=PNG, all present -> png", p, all, D + "splash.png", false);
    check("Format=png, png missing -> error", p, { D + "splash.dds" }, "", true);
    splash::Config j; j.format = "jpeg";
    check("Format=jpeg alias, only .jpeg", j, { D + "splash.jpeg" }, D + "splash.jpeg", false);
    splash::Config bad; bad.format = "gif";
    check("Format=gif -> warn, auto", bad, { D + "splash.png" }, D + "splash.png", false, true);
    splash::Config off; off.enabled = false;
    check("Enabled=0 -> vanilla", off, all, "", false);
    splash::Config f; f.file = " \"data\\fifarna\\winter.png\" ";
    check("File= quoted, exists (overrides format)", f, { "data\\fifarna\\winter.png", D + "splash.fsh" }, "data\\fifarna\\winter.png", false);
    check("File= missing -> error", f, all, "", true);
    printf(fails ? "\n%d FAILURE(S)\n" : "\nALL OK\n", fails);
    return fails != 0;
}
