#pragma once

#include "PluginBase/Modules.h"

#include <convar.h>

class ClientTools : public Module<ClientTools>
{
public:
    ClientTools();

    static bool CheckDependencies();
    static constexpr __forceinline const char* GetModuleName() { return "Client Tools"; }

private:
    void UpdateWindowTitle(const char* oldval);
    void UpdateSeparateConsole();

    ConVar ce_clienttools_windowtitle;
    ConVar ce_clienttools_separate_console;

    std::string m_OriginalTitle;
};
