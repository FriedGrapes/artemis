#include "keyboardhook_win32.h"

#ifdef Q_OS_WIN

#include "settings/hotkeymanager.h"

#include <SDL_syswm.h>
#include <windows.h>

static HHOOK s_Hook = nullptr;
static HWND s_TargetWindow = nullptr;
static SDL_Window* s_SdlWindow = nullptr;
static int s_IgnoreModMask = 0;
static int s_IgnoreVkCode = 0;

// Tracks which of the keys we intercept are currently held, so auto-repeat
// can be flagged as such rather than delivered as a fresh press.
static bool s_KeyDown[256];

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

// The keys we swallow: those the client OS would otherwise act on itself
// while we are capturing system shortcuts.
static SDL_Scancode interceptedScancodeForVk(DWORD vkCode)
{
    switch (vkCode) {
    case VK_LWIN:     return SDL_SCANCODE_LGUI;
    case VK_RWIN:     return SDL_SCANCODE_RGUI;
    case VK_TAB:      return SDL_SCANCODE_TAB;
    case VK_ESCAPE:   return SDL_SCANCODE_ESCAPE;
    case VK_SNAPSHOT: return SDL_SCANCODE_PRINTSCREEN;
    default:          return SDL_SCANCODE_UNKNOWN;
    }
}

// Keeps SDL's modifier state in step with the modifier keys we swallow.
// Without this, SDL would never see the Win key go down, so keys that
// follow it would be reported (and forwarded to the host) without their
// Meta modifier, and Win-based hotkeys could never match.
static void syncModStateForScancode(SDL_Scancode scancode, bool isDown)
{
    SDL_Keymod bit;
    switch (scancode) {
    case SDL_SCANCODE_LGUI: bit = KMOD_LGUI; break;
    case SDL_SCANCODE_RGUI: bit = KMOD_RGUI; break;
    default:
        return;
    }

    SDL_Keymod mod = SDL_GetModState();
    SDL_SetModState((SDL_Keymod)(isDown ? (mod | bit) : (mod & ~bit)));
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

    SDL_Scancode scancode = interceptedScancodeForVk(data->vkCode);
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        // Not ours to intercept; it reaches us normally as the focused window
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isRepeat = isDown && s_KeyDown[data->vkCode & 0xFF];
    s_KeyDown[data->vkCode & 0xFF] = isDown;

    syncModStateForScancode(scancode, isDown);

    // Deliver the key into SDL's event queue directly rather than reposting
    // a window message: posted messages are processed ahead of pending
    // hardware input, which would reorder these keys relative to the ones
    // that reach us normally (e.g. delivering Win down *and up* before the
    // key that was pressed while it was held).
    SDL_Event event = {};
    event.type = isDown ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.timestamp = SDL_GetTicks();
    event.key.windowID = s_SdlWindow != nullptr ? SDL_GetWindowID(s_SdlWindow) : 0;
    event.key.state = isDown ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = isRepeat ? 1 : 0;
    event.key.keysym.scancode = scancode;
    event.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
    event.key.keysym.mod = SDL_GetModState();
    SDL_PushEvent(&event);

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
    s_SdlWindow = window;
    s_IgnoreModMask = ignoreModMask;
    s_IgnoreVkCode = ignoreVkCode;

    if (s_Hook != nullptr) {
        // Already installed; the combo above has been refreshed
        return true;
    }

    SDL_zeroa(s_KeyDown);

    s_Hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboardHookProc, GetModuleHandle(nullptr), 0);
    if (s_Hook == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SetWindowsHookEx() failed: %d", (int)GetLastError());
        s_TargetWindow = nullptr;
        s_SdlWindow = nullptr;
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

        // Don't leave a Win key we swallowed stuck down in SDL's state
        SDL_SetModState((SDL_Keymod)(SDL_GetModState() & ~(KMOD_LGUI | KMOD_RGUI)));
        SDL_zeroa(s_KeyDown);

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Removed keyboard hook");
    }

    s_TargetWindow = nullptr;
    s_SdlWindow = nullptr;
    s_IgnoreModMask = 0;
    s_IgnoreVkCode = 0;
}

void Win32KeyboardHook::resetKeyState()
{
    SDL_zeroa(s_KeyDown);

    // Don't leave a swallowed Win key stuck down in SDL's modifier state,
    // which would add a phantom Meta modifier to every later key and stop
    // any hotkey from matching.
    SDL_SetModState((SDL_Keymod)(SDL_GetModState() & ~(KMOD_LGUI | KMOD_RGUI)));
}

bool Win32KeyboardHook::isKeyPhysicallyDown(int vkCode)
{
    return (GetAsyncKeyState(vkCode) & 0x8000) != 0;
}

bool Win32KeyboardHook::isInstalled()
{
    return s_Hook != nullptr;
}

#endif // Q_OS_WIN
