#include "streaming/session.h"

#include "settings/hotkeymanager.h"
#include "keyboardhook_win32.h"

#include <Limelight.h>
#include "SDL_compat.h"

#define VK_0 0x30
#define VK_A 0x41

// These are real Windows VK_* codes
#ifndef VK_F1
#define VK_F1 0x70
#define VK_F13 0x7C
#define VK_NUMPAD0 0x60
#endif

void SdlInputHandler::performSpecialKeyCombo(KeyCombo combo)
{
    switch (combo) {
    case KeyComboQuit:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected quit key combo");

        // Push a quit event to the main loop
        SDL_Event event;
        event.type = SDL_QUIT;
        event.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&event);
        break;

    case KeyComboUngrabInput:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected mouse capture toggle combo");

        // Stop handling future input
        setCaptureActive(!isCaptureActive());

        // Force raise all keys to ensure they aren't stuck,
        // since we won't get their key up events.
        raiseAllKeys();
        break;

    case KeyComboToggleFullScreen:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected full-screen toggle combo");
        Session::s_ActiveSession->toggleFullscreen();

        // Force raise all keys just be safe across this full-screen/windowed
        // transition just in case key events get lost.
        raiseAllKeys();
        break;

    case KeyComboToggleStatsOverlay:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected stats toggle combo");

        // Toggle the stats overlay
        Session::get()->getOverlayManager().setOverlayState(Overlay::OverlayDebug,
                                                            !Session::get()->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug));
        break;

    case KeyComboToggleMouseMode:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected mouse mode toggle combo");

        // Uncapture input
        setCaptureActive(false);

        // Toggle mouse mode
        m_AbsoluteMouseMode = !m_AbsoluteMouseMode;

        // Recapture input
        setCaptureActive(true);
        break;

    case KeyComboToggleCursorHide:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected show mouse combo");

        if (!SDL_GetRelativeMouseMode()) {
            m_MouseCursorCapturedVisibilityState = !m_MouseCursorCapturedVisibilityState;
            SDL_ShowCursor(m_MouseCursorCapturedVisibilityState);
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Cursor can only be shown in remote desktop mouse mode");
        }
        break;

    case KeyComboToggleMinimize:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected minimize combo");
        SDL_MinimizeWindow(m_Window);
        break;

    case KeyComboPasteText:
    {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected type clipboard text combo");

        // Force raise all keys to ensure that none of them interfere
        // with the text we're going to type.
        raiseAllKeys();

        char* text;
        if (SDL_HasClipboardText() && (text = SDL_GetClipboardText()) != nullptr) {
            // Sending both CR and LF will lead to two newlines in the destination for
            // each newline in the source, so we fix up any CRLFs into just a single LF.
            for (char* c = text; *c != 0; c++) {
                if (*c == '\r' && *(c + 1) == '\n') {
                    // We're using strlen() rather than strlen() - 1 since we need to add 1
                    // to copy the null terminator which is not included in strlen()'s count.
                    memmove(c, c + 1, strlen(c));
                }
            }

            // Send this text to the PC
            LiSendUtf8TextEvent(text, (unsigned int)strlen(text));

            // SDL_GetClipboardText() allocates, so we must free
            SDL_free((void*)text);
        }
        else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "No text in clipboard to paste!");
        }
        break;
    }

    case KeyComboTogglePointerRegionLock:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected pointer region lock toggle combo");
        m_PointerRegionLockActive = !m_PointerRegionLockActive;

        // Remember that the user changed this manually, so we don't mess with it anymore
        // during windowed <-> full-screen transitions.
        m_PointerRegionLockToggledByUser = true;

        // Apply the new region lock
        updatePointerRegionLock();
        break;

    case KeyComboQuitAndExit:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Detected quitAndExit key combo");

        // Indicate that we want to exit afterwards
        Session::get()->setShouldExitAfterQuit();

        // Push a quit event to the main loop
        SDL_Event quitExitEvent;
        quitExitEvent.type = SDL_QUIT;
        quitExitEvent.quit.timestamp = SDL_GetTicks();
        SDL_PushEvent(&quitExitEvent);
        break;

    // KeyComboToggleServerCommands removed - now handled through QuickMenu

    case KeyComboToggleQuickMenu:
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Detected quick menu toggle combo");

        // Access the QuickMenuManager through the Session
        if (Session::get()) {
            Session::get()->toggleQuickMenu();
        }
        break;

    case KeyComboIgnore:
        // Handled inline in handleKeyEvent() since it needs the key event
        // context to suppress the matching key up event too
        break;

    default:
        Q_UNREACHABLE();
    }
}

#ifdef Q_OS_WIN
// True for the Windows virtual-key codes of the modifier keys themselves,
// which must not trigger the re-assert logic below on their own press.
static bool isModifierKeyCode(short keyCode)
{
    switch (keyCode) {
    case 0xA0: // VK_LSHIFT
    case 0xA1: // VK_RSHIFT
    case 0xA2: // VK_LCONTROL
    case 0xA3: // VK_RCONTROL
    case 0xA4: // VK_LMENU
    case 0xA5: // VK_RMENU
    case 0x5B: // VK_LWIN
    case 0x5C: // VK_RWIN
        return true;
    default:
        return false;
    }
}
#endif

#ifdef Q_OS_WIN
// Keeps the modifier state the host believes in agreement with the keys the
// user is physically holding.
//
// Two things pull them apart. Losing focus raises every key on the host (see
// raiseAllKeys()), but a modifier that was never physically released will
// never generate another key down event, so the host keeps believing it is
// up. And once SDL's own keyboard state has been reset by that focus change,
// SDL discards the eventual release as redundant, so the host never learns
// about it either and the modifier stays stuck down instead.
//
// The Ignore Hotkey makes both easy to hit, since switching the client to
// another desktop and back while still holding the combo's modifiers is
// exactly this situation. Ask the OS what is really held rather than SDL,
// whose state is not restored after a focus change.
//
// modifierFlags, when given, is updated to match so the key being forwarded
// carries the right modifiers.
void SdlInputHandler::syncHeldModifiers(char* modifierFlags)
{
    // SDL's keyboard grab swallows the modifier keys themselves before they
    // reach the OS, so while it is the thing capturing, the OS would report
    // every modifier as up no matter what the user is holding. Syncing
    // against that would release modifiers out from under the host. Our own
    // hook deliberately passes them through, so its state is trustworthy.
    if (!m_UsingCustomKeyboardHook && isSystemKeyCaptureActive()) {
        return;
    }

    // Only the modifiers that reach the OS normally can be checked this way.
    // The Win key is deliberately swallowed while we are capturing system
    // keys (by our hook, or by SDL's grab when no Ignore Hotkey is bound),
    // so the OS never sees it go down and would always report it as up.
    // Its state is tracked through the normal key event path instead.
    static const struct {
        int vkCode;
        short keyCode;
        char modifierFlag;
    } k_Modifiers[] = {
        { 0xA2, 0xA2, MODIFIER_CTRL  }, // VK_LCONTROL
        { 0xA3, 0xA3, MODIFIER_CTRL  }, // VK_RCONTROL
        { 0xA0, 0xA0, MODIFIER_SHIFT }, // VK_LSHIFT
        { 0xA1, 0xA1, MODIFIER_SHIFT }, // VK_RSHIFT
        { 0xA4, 0xA4, MODIFIER_ALT   }, // VK_LMENU
        { 0xA5, 0xA5, MODIFIER_ALT   }, // VK_RMENU
    };

    for (auto& modifier : k_Modifiers) {
        bool physicallyDown = Win32KeyboardHook::isKeyPhysicallyDown(modifier.vkCode);
        bool hostThinksDown = m_KeysDown.contains(modifier.keyCode);

        if (physicallyDown && modifierFlags != nullptr) {
            *modifierFlags |= modifier.modifierFlag;
        }

        if (physicallyDown == hostThinksDown) {
            continue;
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Syncing modifier 0x%x to %s",
                    modifier.keyCode, physicallyDown ? "down" : "up");

        if (physicallyDown) {
            m_KeysDown.insert(modifier.keyCode);
            LiSendKeyboardEvent(0x8000 | modifier.keyCode, KEY_ACTION_DOWN, 0);
        }
        else {
            m_KeysDown.remove(modifier.keyCode);
            LiSendKeyboardEvent(0x8000 | modifier.keyCode, KEY_ACTION_UP, 0);
        }
    }
}
#endif

void SdlInputHandler::handleKeyEvent(SDL_KeyboardEvent* event)
{
    short keyCode;
    char modifiers;
    bool shouldNotConvertToScanCodeOnServer = false;

    if (event->repeat) {
        // Ignore repeat key down events
        SDL_assert(event->state == SDL_PRESSED);
        return;
    }

    // If this key's press was suppressed by the ignore hotkey, suppress
    // its release too so the host never sees the key at all.
    if (event->state == SDL_RELEASED &&
            m_IgnoredScancodesDown.remove(event->keysym.scancode)) {
        return;
    }

    // Check for our special key combos
    if (event->state == SDL_PRESSED) {
        // Compute the currently active modifier set. Left/right variants
        // are collapsed together and lock states (Num/Caps) are excluded.
        int activeMods = 0;
        if (event->keysym.mod & KMOD_CTRL) {
            activeMods |= HotkeyManager::HkModCtrl;
        }
        if (event->keysym.mod & KMOD_ALT) {
            activeMods |= HotkeyManager::HkModAlt;
        }
        if (event->keysym.mod & KMOD_SHIFT) {
            activeMods |= HotkeyManager::HkModShift;
        }
        if (event->keysym.mod & KMOD_GUI) {
            activeMods |= HotkeyManager::HkModGui;
        }

        // Valid combos always include at least one modifier, and a combo
        // only matches when exactly its bound modifiers are held.
        if (activeMods != 0) {
            // The ignore hotkey takes precedence over all other combos to
            // guarantee its key is never forwarded to the host.
            const auto& ignoreCombo = m_SpecialKeyCombos[KeyComboIgnore];
            if (ignoreCombo.enabled && ignoreCombo.modMask == activeMods &&
                    (event->keysym.sym == ignoreCombo.keyCode ||
                     (ignoreCombo.scanCode != SDL_SCANCODE_UNKNOWN &&
                      event->keysym.scancode == ignoreCombo.scanCode))) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Detected ignore hotkey - withholding key from host");
                m_IgnoredScancodesDown.insert(event->keysym.scancode);
                return;
            }

            // First we test the SDLK combos for matches,
            // that way we ensure that latin keyboard users
            // can match to the key they see on their keyboards.
            // If nothing matches that, we'll then go on to
            // checking scancodes so non-latin keyboard users
            // can have working hotkeys (though possibly in
            // odd positions). We must do all SDLK tests before
            // any scancode tests to avoid issues in cases
            // where the SDLK for one shortcut collides with
            // the scancode of another.

            for (int i = 0; i < KeyComboMax; i++) {
                if (i != KeyComboIgnore &&
                        m_SpecialKeyCombos[i].enabled &&
                        m_SpecialKeyCombos[i].modMask == activeMods &&
                        event->keysym.sym == m_SpecialKeyCombos[i].keyCode) {
                    performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo);
                    return;
                }
            }

            for (int i = 0; i < KeyComboMax; i++) {
                if (i != KeyComboIgnore &&
                        m_SpecialKeyCombos[i].enabled &&
                        m_SpecialKeyCombos[i].modMask == activeMods &&
                        m_SpecialKeyCombos[i].scanCode != SDL_SCANCODE_UNKNOWN &&
                        event->keysym.scancode == m_SpecialKeyCombos[i].scanCode) {
                    performSpecialKeyCombo(m_SpecialKeyCombos[i].keyCombo);
                    return;
                }
            }
        }
    }

    // Set modifier flags
    modifiers = 0;
    if (event->keysym.mod & KMOD_CTRL) {
        modifiers |= MODIFIER_CTRL;
    }
    if (event->keysym.mod & KMOD_ALT) {
        modifiers |= MODIFIER_ALT;
    }
    if (event->keysym.mod & KMOD_SHIFT) {
        modifiers |= MODIFIER_SHIFT;
    }
    if (event->keysym.mod & KMOD_GUI) {
        if (isSystemKeyCaptureActive()) {
            modifiers |= MODIFIER_META;
        }
    }

    // Set keycode. We explicitly use scancode here because GFE will try to correct
    // for AZERTY layouts on the host but it depends on receiving VK_ values matching
    // a QWERTY layout to work.
    if (event->keysym.scancode >= SDL_SCANCODE_1 && event->keysym.scancode <= SDL_SCANCODE_9) {
        // SDL defines SDL_SCANCODE_0 > SDL_SCANCODE_9, so we need to handle that manually
        keyCode = (event->keysym.scancode - SDL_SCANCODE_1) + VK_0 + 1;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_A && event->keysym.scancode <= SDL_SCANCODE_Z) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_A) + VK_A;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_F1 && event->keysym.scancode <= SDL_SCANCODE_F12) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_F1) + VK_F1;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_F13 && event->keysym.scancode <= SDL_SCANCODE_F24) {
        keyCode = (event->keysym.scancode - SDL_SCANCODE_F13) + VK_F13;
    }
    else if (event->keysym.scancode >= SDL_SCANCODE_KP_1 && event->keysym.scancode <= SDL_SCANCODE_KP_9) {
        // SDL defines SDL_SCANCODE_KP_0 > SDL_SCANCODE_KP_9, so we need to handle that manually
        keyCode = (event->keysym.scancode - SDL_SCANCODE_KP_1) + VK_NUMPAD0 + 1;
    }
    else {
        switch (event->keysym.scancode) {
            case SDL_SCANCODE_BACKSPACE:
                keyCode = 0x08;
                break;
            case SDL_SCANCODE_TAB:
                keyCode = 0x09;
                break;
            case SDL_SCANCODE_CLEAR:
                keyCode = 0x0C;
                break;
            case SDL_SCANCODE_KP_ENTER: // FIXME: Is this correct?
            case SDL_SCANCODE_RETURN:
                keyCode = 0x0D;
                break;
            case SDL_SCANCODE_PAUSE:
                keyCode = 0x13;
                break;
            case SDL_SCANCODE_CAPSLOCK:
                keyCode = 0x14;
                break;
            case SDL_SCANCODE_ESCAPE:
                keyCode = 0x1B;
                break;
            case SDL_SCANCODE_SPACE:
                keyCode = 0x20;
                break;
            case SDL_SCANCODE_PAGEUP:
                keyCode = 0x21;
                break;
            case SDL_SCANCODE_PAGEDOWN:
                keyCode = 0x22;
                break;
            case SDL_SCANCODE_END:
                keyCode = 0x23;
                break;
            case SDL_SCANCODE_HOME:
                keyCode = 0x24;
                break;
            case SDL_SCANCODE_LEFT:
                keyCode = 0x25;
                break;
            case SDL_SCANCODE_UP:
                keyCode = 0x26;
                break;
            case SDL_SCANCODE_RIGHT:
                keyCode = 0x27;
                break;
            case SDL_SCANCODE_DOWN:
                keyCode = 0x28;
                break;
            case SDL_SCANCODE_SELECT:
                keyCode = 0x29;
                break;
            case SDL_SCANCODE_EXECUTE:
                keyCode = 0x2B;
                break;
            case SDL_SCANCODE_PRINTSCREEN:
                keyCode = 0x2C;
                break;
            case SDL_SCANCODE_INSERT:
                keyCode = 0x2D;
                break;
            case SDL_SCANCODE_DELETE:
                keyCode = 0x2E;
                break;
            case SDL_SCANCODE_HELP:
                keyCode = 0x2F;
                break;
            case SDL_SCANCODE_KP_0:
                // See comment above about why we only handle SDL_SCANCODE_KP_0 here
                keyCode = VK_NUMPAD0;
                break;
            case SDL_SCANCODE_0:
                // See comment above about why we only handle SDL_SCANCODE_0 here
                keyCode = VK_0;
                break;
            case SDL_SCANCODE_KP_MULTIPLY:
                keyCode = 0x6A;
                break;
            case SDL_SCANCODE_KP_PLUS:
                keyCode = 0x6B;
                break;
            case SDL_SCANCODE_KP_COMMA:
                keyCode = 0x6C;
                break;
            case SDL_SCANCODE_KP_MINUS:
                keyCode = 0x6D;
                break;
            case SDL_SCANCODE_KP_PERIOD:
                keyCode = 0x6E;
                break;
            case SDL_SCANCODE_KP_DIVIDE:
                keyCode = 0x6F;
                break;
            case SDL_SCANCODE_NUMLOCKCLEAR:
                keyCode = 0x90;
                break;
            case SDL_SCANCODE_SCROLLLOCK:
                keyCode = 0x91;
                break;
            case SDL_SCANCODE_LSHIFT:
                keyCode = 0xA0;
                break;
            case SDL_SCANCODE_RSHIFT:
                keyCode = 0xA1;
                break;
            case SDL_SCANCODE_LCTRL:
                keyCode = 0xA2;
                break;
            case SDL_SCANCODE_RCTRL:
                keyCode = 0xA3;
                break;
            case SDL_SCANCODE_LALT:
                keyCode = 0xA4;
                break;
            case SDL_SCANCODE_RALT:
                keyCode = 0xA5;
                break;
            case SDL_SCANCODE_LGUI:
                if (!isSystemKeyCaptureActive()) {
                    return;
                }
                keyCode = 0x5B;
                break;
            case SDL_SCANCODE_RGUI:
                if (!isSystemKeyCaptureActive()) {
                    return;
                }
                keyCode = 0x5C;
                break;
            case SDL_SCANCODE_APPLICATION:
                keyCode = 0x5D;
                break;
            case SDL_SCANCODE_AC_BACK:
                keyCode = 0xA6;
                break;
            case SDL_SCANCODE_AC_FORWARD:
                keyCode = 0xA7;
                break;
            case SDL_SCANCODE_AC_REFRESH:
                keyCode = 0xA8;
                break;
            case SDL_SCANCODE_AC_STOP:
                keyCode = 0xA9;
                break;
            case SDL_SCANCODE_AC_SEARCH:
                keyCode = 0xAA;
                break;
            case SDL_SCANCODE_AC_BOOKMARKS:
                keyCode = 0xAB;
                break;
            case SDL_SCANCODE_AC_HOME:
                keyCode = 0xAC;
                break;
            case SDL_SCANCODE_SEMICOLON:
                keyCode = 0xBA;
                break;
            case SDL_SCANCODE_EQUALS:
                keyCode = 0xBB;
                break;
            case SDL_SCANCODE_COMMA:
                keyCode = 0xBC;
                break;
            case SDL_SCANCODE_MINUS:
                keyCode = 0xBD;
                break;
            case SDL_SCANCODE_PERIOD:
                keyCode = 0xBE;
                break;
            case SDL_SCANCODE_SLASH:
                keyCode = 0xBF;
                break;
            case SDL_SCANCODE_GRAVE:
                keyCode = 0xC0;
                break;
            case SDL_SCANCODE_LEFTBRACKET:
                keyCode = 0xDB;
                break;
            case SDL_SCANCODE_INTERNATIONAL3:
                shouldNotConvertToScanCodeOnServer = true;
                [[fallthrough]];
            case SDL_SCANCODE_BACKSLASH:
                keyCode = 0xDC;
                break;
            case SDL_SCANCODE_RIGHTBRACKET:
                keyCode = 0xDD;
                break;
            case SDL_SCANCODE_APOSTROPHE:
                keyCode = 0xDE;
                break;
            case SDL_SCANCODE_INTERNATIONAL1:
                shouldNotConvertToScanCodeOnServer = true;
                [[fallthrough]];
            case SDL_SCANCODE_NONUSBACKSLASH:
                keyCode = 0xE2;
                break;
            case SDL_SCANCODE_LANG1:
                keyCode = 0x1C;
                break;
            case SDL_SCANCODE_LANG2:
                keyCode = 0x1D;
                break;
            default:
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Unhandled button event: %d",
                             event->keysym.scancode);
                return;
        }
    }

#ifdef Q_OS_WIN
    if (!isModifierKeyCode(keyCode)) {
        syncHeldModifiers(&modifiers);
    }
#endif

    // Track the key state so we always know which keys are down
    if (event->state == SDL_PRESSED) {
        m_KeysDown.insert(keyCode);
    }
    else {
        m_KeysDown.remove(keyCode);
    }

    LiSendKeyboardEvent2(0x8000 | keyCode,
                        event->state == SDL_PRESSED ?
                            KEY_ACTION_DOWN : KEY_ACTION_UP,
                        modifiers,
                        shouldNotConvertToScanCodeOnServer ? SS_KBE_FLAG_NON_NORMALIZED : 0);
}
