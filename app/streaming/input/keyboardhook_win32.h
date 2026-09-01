#pragma once

#include <QtGlobal>

#ifdef Q_OS_WIN

#include "SDL_compat.h"

// Windows-only low-level keyboard hook used in place of SDL's keyboard grab
// while an Ignore Hotkey is bound.
//
// SDL's grab is all-or-nothing: it installs its own WH_KEYBOARD_LL hook and
// swallows the keys it intercepts system-wide, so software sitting further
// down the hook chain (AutoHotkey, push-to-talk tools, and the like) never
// observes them. There is no SDL API to exempt a combo from that.
//
// This hook replaces it and makes the decision per keystroke instead:
//
//   - keys belonging to the bound Ignore Hotkey are passed down the chain,
//     so local client-side software sees them normally (the host is still
//     protected from them by SdlInputHandler's existing suppression)
//   - the reserved keys that would otherwise be handled by the client OS
//     (Win, Tab, Escape) are swallowed and reposted to our own window, so
//     they reach the host exactly as they do under SDL's grab
//   - everything else is passed through untouched and reaches us normally
//     as the focused window
//
// All of this is inert on other platforms: the entire class is compiled out.
class Win32KeyboardHook
{
public:
    // Installs the hook (or updates the combo of an already-installed hook).
    // modMask uses HotkeyManager::HotkeyModifier bits; vkCode is a Windows
    // virtual-key code. Must be called from the thread running the message
    // loop. Returns false if the hook could not be installed.
    static bool install(SDL_Window* window, int ignoreModMask, int ignoreVkCode);

    static void uninstall();

    // True if the given Windows virtual-key code is physically held right
    // now, according to the OS rather than SDL's own bookkeeping (which is
    // reset across focus changes and cannot be relied on afterwards).
    static bool isKeyPhysicallyDown(int vkCode);

    // Forgets the state of the keys we swallow. Called when the window loses
    // focus, since a key held at that moment (Win+L is the obvious case) will
    // never deliver its release to us, and the stale state would otherwise
    // make the next press look like an auto-repeat and leave SDL believing a
    // modifier is still held.
    static void resetKeyState();

    static bool isInstalled();
};

#endif // Q_OS_WIN
