#pragma once
#include "plugin.h"

class FifaDebug {
    HMODULE hDebugModule = 0;
    uintptr_t gFifaDebugPrint = 0;
public:
    FifaDebug() {
        hDebugModule = LoadLibraryA("plugins\\FifaDebug.asi");
        if (hDebugModule)
            gFifaDebugPrint = (uintptr_t)GetProcAddress(hDebugModule, "DebugPrint");
    }
    ~FifaDebug() {
        if (hDebugModule)
            FreeLibrary(hDebugModule);
    }
    void Print(char const *text) {
        if (gFifaDebugPrint)
            plugin::CallDynGlobal(gFifaDebugPrint, text);
    }

    static FifaDebug &GetInstance() {
        static FifaDebug fifamDebug;
        return fifamDebug;
    }
};

inline void DebugPrint(std::string const &message) {
    FifaDebug::GetInstance().Print(message.c_str());
}
