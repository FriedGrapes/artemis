#include "keyboardhook_win32.h"

#ifdef Q_OS_WIN

#include "settings/hotkeymanager.h"

#include <SDL_syswm.h>
#include <windows.h>

static HHOOK s_Hook = nullptr;
static HWND s_TargetWindow = nullptr;
static int s_IgnoreModMask = 0;
static int s_IgnoreVkCode = 0;

// Collapses the left/right variants of a modifier key into the
// HotkeyManager modifier bit it represents (0 if it isn't a modifier).
static int modifierBitForVk(DWORD vkCode)
{
    switch (vkCode) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
        return HotkeyManager::HkModCtrl;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
        return HotkeyManager::HkModAlt;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        return HotkeyManager::HkModShift;
    case VK_LWIN:
    case VK_RWIN:
        return HotkeyManager::HkModGui;
    default:
        return 0;
    }
}

// True if this key is part of the bound Ignore Hotkey: either its final key
// or one of the modifiers it uses. Those keys must reach the rest of the
// system so client-side software can act on the combo.
static bool isIgnoreComboKey(DWORD vkCode)
{
    if (s_IgnoreVkCode == 0 || s_IgnoreModMask == 0) {
        return false;
    }

    if ((int)vkCode == s_IgnoreVkCode) {
        return true;
    }

    int modBit = modifierBitForVk(vkCode);
    return modBit != 0 && (s_IgnoreModMask & modBit) != 0;
}

// Rebuilds the lParam of the original WM_KEY* message from the hook data so
// the reposted message is indistinguishable from the real one.
static LPARAM rebuildLParam(const KBDLLHOOKSTRUCT* data, bool isDown)
{
    LPARAM lParam = 1; // repeat count

    lParam |= (LPARAM)(data->scanCode & 0xFF) << 16;

    if (data->flags & LLKHF_EXTENDED) {
        lParam |= 1ll << 24;
    }
    if (data->flags & LLKHF_ALTDOWN) {
        lParam |= 1ll << 29;
    }
    if (!isDown) {
        // Previous key state and transition state are both set on release
        lParam |= (1ll << 30) | (1ll << 31);
    }

    return lParam;
}

static LRESULT CALLBACK keyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION || s_TargetWindow == nullptr) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    auto data = (KBDLLHOOKSTRUCT*)lParam;

    // Never interfere with synthetic input. Besides avoiding loops if we
    // ever inject anything ourselves, this keeps tools that drive the
    // client programmatically working as they do today.
    if (data->flags & LLKHF_INJECTED) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // Only intercept while our window is the foreground window. Otherwise
    // the user is interacting with something else on the client and every
    // key must behave normally.
    if (GetForegroundWindow() != s_TargetWindow) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // The Ignore Hotkey's keys are deliberately left for the rest of the
    // system. We still receive them ourselves as the focused window, and
    // SdlInputHandler withholds them from the host.
    if (isIgnoreComboKey(data->vkCode)) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // Swallow the keys the client OS would otherwise act on itself, and
    // repost them to our window so they are forwarded to the host. This is
    // the same set SDL's keyboard grab intercepts.
    switch (data->vkCode) {
    case VK_LWIN:
    case VK_RWIN:
    case VK_TAB:
    case VK_ESCAPE:
        break;
    default:
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    PostMessage(s_TargetWindow, (UINT)wParam, data->vkCode, rebuildLParam(data, isDown));
    return 1;
}

bool Win32KeyboardHook::install(SDL_Window* window, int ignoreModMask, int ignoreVkCode)
{
    if (window == nullptr) {
        return false;
    }

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info) || info.subsystem != SDL_SYSWM_WINDOWS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Unable to get native window handle for keyboard hook");
        return false;
    }

    s_TargetWindow = info.info.win.window;
    s_IgnoreModMask = ignoreModMask;
    s_IgnoreVkCode = ignoreVkCode;

    if (s_Hook != nullptr) {
        // Already installed; the combo above has been refreshed
        return true;
    }

    s_Hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboardHookProc, GetModuleHandle(nullptr), 0);
    if (s_Hook == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SetWindowsHookEx() failed: %d", (int)GetLastError());
        s_TargetWindow = nullptr;
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "Installed keyboard hook with pass-through for VK 0x%x (mods 0x%x)",
                ignoreVkCode, ignoreModMask);
    return true;
}

void Win32KeyboardHook::uninstall()
{
    if (s_Hook != nullptr) {
        UnhookWindowsHookEx(s_Hook);
        s_Hook = nullptr;

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Removed keyboard hook");
    }

    s_TargetWindow = nullptr;
    s_IgnoreModMask = 0;
    s_IgnoreVkCode = 0;
}

bool Win32KeyboardHook::isInstalled()
{
    return s_Hook != nullptr;
}

#endif // Q_OS_WIN
