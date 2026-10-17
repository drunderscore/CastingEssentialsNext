#include "Modules/ClientTools.h"
#include "PluginBase/HookManager.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

MODULE_REGISTER(ClientTools);

ClientTools::ClientTools()
    : ce_clienttools_windowtitle(
          "ce_clienttools_windowtitle", "", FCVAR_NONE, "Overrides the game window title",
          [](IConVar* var, const char* oldval, float foldval) { GetModule()->UpdateWindowTitle(oldval); })
{
}

// The game's window is the largest visible top-level window this process owns. Asking Windows directly means no
// dependency on the game's internals, so no game update can break it.
static HWND GetMainWindow()
{
    static HWND s_Window = nullptr;
    if (s_Window && IsWindow(s_Window))
        return s_Window;

    struct Search
    {
        HWND m_Best = nullptr;
        LONG m_BestArea = 0;
    } search;

    EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            DWORD processID;
            GetWindowThreadProcessId(window, &processID);
            if (processID != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER))
                return TRUE;

            RECT rect;
            if (!GetWindowRect(window, &rect))
                return TRUE;

            auto& search = *reinterpret_cast<Search*>(param);
            const auto area = (rect.right - rect.left) * (rect.bottom - rect.top);
            if (area > search.m_BestArea)
            {
                search.m_Best = window;
                search.m_BestArea = area;
            }

            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));

    s_Window = search.m_Best;
    return s_Window;
}

bool ClientTools::CheckDependencies() { return true; }

void ClientTools::UpdateWindowTitle(const char* oldval)
{
    const auto window = GetMainWindow();
    if (!window)
    {
        PluginWarning("%s: Unable to find the game window.\n", ce_clienttools_windowtitle.GetName());
        return;
    }

    auto newval = ce_clienttools_windowtitle.GetString();
    if (!newval[0])
    {
        if (oldval[0])
            SetWindowText(window, m_OriginalTitle.c_str());
    }
    else
    {
        if (m_OriginalTitle.empty())
        {
            const auto length = GetWindowTextLength(window) + 1;
            m_OriginalTitle.resize(length);
            GetWindowText(window, m_OriginalTitle.data(), length);
            m_OriginalTitle.resize(strlen(m_OriginalTitle.c_str()));
        }

        SetWindowText(window, newval);
    }
}