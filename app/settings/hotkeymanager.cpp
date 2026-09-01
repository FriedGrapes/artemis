#include "hotkeymanager.h"

#include "SDL_compat.h"

#include <QSettings>
#include <QKeySequence>

#define SER_HOTKEYS_GROUP "hotkeys"
#define SER_KEY_SUFFIX "_key"
#define SER_MODS_SUFFIX "_mods"

#define HK_DEFAULT_MODS (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)

struct HotkeyActionInfo
{
    const char* settingsKey;
    const char* name;
    const char* description;
    int defaultMods;
    int defaultKey;
};

// NOTE: Order must match HotkeyManager::HotkeyAction (and therefore
// SdlInputHandler::KeyCombo).
static const HotkeyActionInfo k_Actions[HotkeyManager::ActionCount] = {
    { "quit",
      QT_TRANSLATE_NOOP("HotkeyManager", "Disconnect from session"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Disconnects from the current streaming session"),
      HK_DEFAULT_MODS, Qt::Key_Q },
    { "ungrab",
      QT_TRANSLATE_NOOP("HotkeyManager", "Toggle mouse capture"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Releases or recaptures the mouse and keyboard from the streaming session"),
      HK_DEFAULT_MODS, Qt::Key_Z },
    { "fullscreen",
      QT_TRANSLATE_NOOP("HotkeyManager", "Toggle fullscreen"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Switches between fullscreen and windowed mode"),
      HK_DEFAULT_MODS, Qt::Key_X },
    { "stats",
      QT_TRANSLATE_NOOP("HotkeyManager", "Toggle performance stats overlay"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Shows or hides the real-time performance statistics overlay"),
      HK_DEFAULT_MODS, Qt::Key_S },
    { "mousemode",
      QT_TRANSLATE_NOOP("HotkeyManager", "Toggle mouse mode"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Switches the mouse between remote desktop mode and game mode"),
      HK_DEFAULT_MODS, Qt::Key_M },
    { "cursorhide",
      QT_TRANSLATE_NOOP("HotkeyManager", "Toggle local cursor visibility"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Shows or hides the local mouse cursor in remote desktop mouse mode"),
      HK_DEFAULT_MODS, Qt::Key_C },
    { "minimize",
      QT_TRANSLATE_NOOP("HotkeyManager", "Minimize window"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Minimizes the streaming window"),
      HK_DEFAULT_MODS, Qt::Key_D },
    { "pastetext",
      QT_TRANSLATE_NOOP("HotkeyManager", "Type clipboard text"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Types the text on the client's clipboard into the host"),
      HK_DEFAULT_MODS, Qt::Key_V },
    { "regionlock",
      QT_TRANSLATE_NOOP("HotkeyManager", "Toggle pointer region lock"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Locks or unlocks the mouse pointer to the streaming window"),
      HK_DEFAULT_MODS, Qt::Key_L },
    { "quitexit",
      QT_TRANSLATE_NOOP("HotkeyManager", "Disconnect and quit Artemis"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Disconnects from the session and exits Artemis"),
      HK_DEFAULT_MODS, Qt::Key_E },
    { "quickmenu",
      QT_TRANSLATE_NOOP("HotkeyManager", "Toggle Quick Menu"),
      QT_TRANSLATE_NOOP("HotkeyManager", "Shows or hides the in-stream Quick Menu"),
      HK_DEFAULT_MODS, Qt::Key_Backslash },
    { "ignore",
      QT_TRANSLATE_NOOP("HotkeyManager", "Ignore Hotkey"),
      QT_TRANSLATE_NOOP("HotkeyManager", "This key combo is never sent to the host and is left for the client PC to process, "
                                         "even when capturing system keyboard shortcuts. Useful for local push-to-talk keys, "
                                         "AutoHotkey scripts, and similar client-side tools. Unbound by default. "
                                         "The Win key cannot be used as a modifier for this combo."),
      0, 0 },
};

static HotkeyManager* s_Instance = nullptr;

HotkeyManager::HotkeyManager(QObject* parent)
    : QObject(parent),
      m_Revision(0)
{
}

HotkeyManager* HotkeyManager::get()
{
    // Always created on the main thread at startup (QML type registration),
    // so no locking is required here.
    if (s_Instance == nullptr) {
        s_Instance = new HotkeyManager();
    }
    return s_Instance;
}

int HotkeyManager::getActionCount()
{
    return ActionCount;
}

QString HotkeyManager::getActionName(int action)
{
    if (action < 0 || action >= ActionCount) {
        return QString();
    }
    return tr(k_Actions[action].name);
}

QString HotkeyManager::getActionDescription(int action)
{
    if (action < 0 || action >= ActionCount) {
        return QString();
    }
    return tr(k_Actions[action].description);
}

bool HotkeyManager::isActionSupported(int action)
{
    if (action < 0 || action >= ActionCount) {
        return false;
    }

    // The Ignore Hotkey needs a low-level keyboard hook to hold its combo
    // back from the client OS's system-key capture; that only exists on
    // Windows. Everywhere else the action would be inert, so hide it.
    if (action == ActionIgnore) {
#if defined(Q_OS_WIN)
        return true;
#else
        return false;
#endif
    }

    return true;
}

QString HotkeyManager::getDisplayString(int action)
{
    int mods, key;
    if (!getBinding(action, mods, key)) {
        return QString();
    }
    return formatCombo(mods, key);
}

static int normalizeQtKey(int qtKey, bool keypad);

QString HotkeyManager::formatCombo(int qtModifiers, int qtKey)
{
    QString result;

    // Display the key we'll actually match at stream time (e.g. Shift+2
    // arrives from Qt as '@' on a US layout, but is matched and shown as '2')
    qtKey = normalizeQtKey(qtKey, (qtModifiers & Qt::KeypadModifier) != 0);

    if (qtModifiers & Qt::ControlModifier) {
        result += tr("Ctrl") + "+";
    }
    if (qtModifiers & Qt::AltModifier) {
        result += tr("Alt") + "+";
    }
    if (qtModifiers & Qt::ShiftModifier) {
        result += tr("Shift") + "+";
    }
    if (qtModifiers & Qt::MetaModifier) {
#if defined(Q_OS_WIN)
        result += tr("Win") + "+";
#elif defined(Q_OS_DARWIN)
        result += tr("Cmd") + "+";
#else
        result += tr("Super") + "+";
#endif
    }

    if (qtKey != 0) {
        QString keyName = QKeySequence(qtKey).toString(QKeySequence::PortableText);
        if (keyName.isEmpty()) {
            keyName = tr("Unknown");
        }
        if (qtModifiers & Qt::KeypadModifier) {
            keyName = tr("Num") + " " + keyName;
        }
        result += keyName;
    }

    // With no final key, leave the trailing '+' to show the combo is
    // still being built (e.g. "Ctrl+Alt+")
    return result;
}

// Qt reports the shifted symbol for symbol keys (e.g. Shift+2 arrives as
// Qt::Key_At on a US layout), but SDL reports the unshifted symbol at
// stream time. Normalize the common US-layout shifted symbols back to
// their unshifted keys so the combo can match.
static int normalizeQtKey(int qtKey, bool keypad)
{
    if (keypad) {
        // Keypad keys are not affected by Shift symbol substitution
        return qtKey;
    }

    switch (qtKey) {
    case Qt::Key_Exclam:      return Qt::Key_1;
    case Qt::Key_At:          return Qt::Key_2;
    case Qt::Key_NumberSign:  return Qt::Key_3;
    case Qt::Key_Dollar:      return Qt::Key_4;
    case Qt::Key_Percent:     return Qt::Key_5;
    case Qt::Key_AsciiCircum: return Qt::Key_6;
    case Qt::Key_Ampersand:   return Qt::Key_7;
    case Qt::Key_Asterisk:    return Qt::Key_8;
    case Qt::Key_ParenLeft:   return Qt::Key_9;
    case Qt::Key_ParenRight:  return Qt::Key_0;
    case Qt::Key_Underscore:  return Qt::Key_Minus;
    case Qt::Key_Plus:        return Qt::Key_Equal;
    case Qt::Key_BraceLeft:   return Qt::Key_BracketLeft;
    case Qt::Key_BraceRight:  return Qt::Key_BracketRight;
    case Qt::Key_Bar:         return Qt::Key_Backslash;
    case Qt::Key_Colon:       return Qt::Key_Semicolon;
    case Qt::Key_QuoteDbl:    return Qt::Key_Apostrophe;
    case Qt::Key_Less:        return Qt::Key_Comma;
    case Qt::Key_Greater:     return Qt::Key_Period;
    case Qt::Key_Question:    return Qt::Key_Slash;
    case Qt::Key_AsciiTilde:  return Qt::Key_QuoteLeft;
    case Qt::Key_Backtab:     return Qt::Key_Tab;
    default:                  return qtKey;
    }
}

int HotkeyManager::qtKeyToSdlKeycode(int qtKey, int qtModifiers)
{
    bool keypad = (qtModifiers & Qt::KeypadModifier) != 0;

    qtKey = normalizeQtKey(qtKey, keypad);

    // Letters: Qt uses uppercase ASCII values, SDL uses lowercase
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return SDLK_a + (qtKey - Qt::Key_A);
    }

    if (keypad) {
        switch (qtKey) {
        case Qt::Key_0: return SDLK_KP_0;
        case Qt::Key_1: return SDLK_KP_1;
        case Qt::Key_2: return SDLK_KP_2;
        case Qt::Key_3: return SDLK_KP_3;
        case Qt::Key_4: return SDLK_KP_4;
        case Qt::Key_5: return SDLK_KP_5;
        case Qt::Key_6: return SDLK_KP_6;
        case Qt::Key_7: return SDLK_KP_7;
        case Qt::Key_8: return SDLK_KP_8;
        case Qt::Key_9: return SDLK_KP_9;
        case Qt::Key_Asterisk: return SDLK_KP_MULTIPLY;
        case Qt::Key_Plus:     return SDLK_KP_PLUS;
        case Qt::Key_Minus:    return SDLK_KP_MINUS;
        case Qt::Key_Slash:    return SDLK_KP_DIVIDE;
        case Qt::Key_Period:   return SDLK_KP_PERIOD;
        case Qt::Key_Comma:    return SDLK_KP_COMMA;
        default:
            // Fall through for keypad navigation keys (Home, End, arrows,
            // etc. with Num Lock off), which map like their regular
            // counterparts below
            break;
        }
    }

    // Function keys (contiguous in both Qt and SDL)
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12) {
        return SDLK_F1 + (qtKey - Qt::Key_F1);
    }
    if (qtKey >= Qt::Key_F13 && qtKey <= Qt::Key_F24) {
        return SDLK_F13 + (qtKey - Qt::Key_F13);
    }

    switch (qtKey) {
    case Qt::Key_Escape:     return SDLK_ESCAPE;
    case Qt::Key_Tab:        return SDLK_TAB;
    case Qt::Key_Backspace:  return SDLK_BACKSPACE;
    case Qt::Key_Insert:     return SDLK_INSERT;
    case Qt::Key_Delete:     return SDLK_DELETE;
    case Qt::Key_Pause:      return SDLK_PAUSE;
    case Qt::Key_Print:      return SDLK_PRINTSCREEN;
    case Qt::Key_SysReq:     return SDLK_PRINTSCREEN;
    case Qt::Key_Clear:      return SDLK_CLEAR;
    case Qt::Key_Home:       return SDLK_HOME;
    case Qt::Key_End:        return SDLK_END;
    case Qt::Key_Left:       return SDLK_LEFT;
    case Qt::Key_Up:         return SDLK_UP;
    case Qt::Key_Right:      return SDLK_RIGHT;
    case Qt::Key_Down:       return SDLK_DOWN;
    case Qt::Key_PageUp:     return SDLK_PAGEUP;
    case Qt::Key_PageDown:   return SDLK_PAGEDOWN;
    case Qt::Key_CapsLock:   return SDLK_CAPSLOCK;
    case Qt::Key_NumLock:    return SDLK_NUMLOCKCLEAR;
    case Qt::Key_ScrollLock: return SDLK_SCROLLLOCK;
    case Qt::Key_Menu:       return SDLK_APPLICATION;
    default:
        break;
    }

    // Printable Latin-1 keys: Qt key codes are the (uppercased) character
    // values and SDL keycodes are the lowercase character values. ASCII
    // letters and digits were already handled above.
    if (qtKey >= 0x20 && qtKey <= 0xFF) {
        // Lowercase Latin-1 uppercase letters (0xD7 is the multiplication sign)
        if (qtKey >= 0xC0 && qtKey <= 0xDE && qtKey != 0xD7) {
            return qtKey + 0x20;
        }
        return qtKey;
    }

    return SDLK_UNKNOWN;
}

#if defined(Q_OS_WIN)
int HotkeyManager::qtKeyToWindowsVk(int qtKey, int qtModifiers)
{
    bool keypad = (qtModifiers & Qt::KeypadModifier) != 0;

    qtKey = normalizeQtKey(qtKey, keypad);

    // Letters and digits share their values between Qt keys and Windows VKs
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return qtKey;
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        return keypad ? (0x60 + (qtKey - Qt::Key_0)) : qtKey;
    }

    if (keypad) {
        switch (qtKey) {
        case Qt::Key_Asterisk: return 0x6A; // VK_MULTIPLY
        case Qt::Key_Plus:     return 0x6B; // VK_ADD
        case Qt::Key_Minus:    return 0x6D; // VK_SUBTRACT
        case Qt::Key_Period:   return 0x6E; // VK_DECIMAL
        case Qt::Key_Slash:    return 0x6F; // VK_DIVIDE
        default:
            break;
        }
    }

    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) {
        return 0x70 + (qtKey - Qt::Key_F1); // VK_F1..VK_F24
    }

    switch (qtKey) {
    case Qt::Key_Backspace:    return 0x08;
    case Qt::Key_Tab:          return 0x09;
    case Qt::Key_Clear:        return 0x0C;
    case Qt::Key_Escape:       return 0x1B;
    case Qt::Key_Space:        return 0x20;
    case Qt::Key_PageUp:       return 0x21;
    case Qt::Key_PageDown:     return 0x22;
    case Qt::Key_End:          return 0x23;
    case Qt::Key_Home:         return 0x24;
    case Qt::Key_Left:         return 0x25;
    case Qt::Key_Up:           return 0x26;
    case Qt::Key_Right:        return 0x27;
    case Qt::Key_Down:         return 0x28;
    case Qt::Key_Print:        return 0x2C;
    case Qt::Key_Insert:       return 0x2D;
    case Qt::Key_Delete:       return 0x2E;
    case Qt::Key_Menu:         return 0x5D;
    case Qt::Key_NumLock:      return 0x90;
    case Qt::Key_ScrollLock:   return 0x91;
    case Qt::Key_Semicolon:    return 0xBA;
    case Qt::Key_Equal:        return 0xBB;
    case Qt::Key_Comma:        return 0xBC;
    case Qt::Key_Minus:        return 0xBD;
    case Qt::Key_Period:       return 0xBE;
    case Qt::Key_Slash:        return 0xBF;
    case Qt::Key_QuoteLeft:    return 0xC0;
    case Qt::Key_BracketLeft:  return 0xDB;
    case Qt::Key_Backslash:    return 0xDC;
    case Qt::Key_BracketRight: return 0xDD;
    case Qt::Key_Apostrophe:   return 0xDE;
    default:                   return 0;
    }
}
#endif

int HotkeyManager::sdlKeycodeToDefaultScancode(int sdlKey)
{
    // Non-printable SDL keycodes directly encode their scancode
    if (sdlKey & SDLK_SCANCODE_MASK) {
        return sdlKey & ~SDLK_SCANCODE_MASK;
    }

    // Letters
    if (sdlKey >= SDLK_a && sdlKey <= SDLK_z) {
        return SDL_SCANCODE_A + (sdlKey - SDLK_a);
    }

    // Digits (SDL_SCANCODE_0 > SDL_SCANCODE_9, so 0 is handled below)
    if (sdlKey >= SDLK_1 && sdlKey <= SDLK_9) {
        return SDL_SCANCODE_1 + (sdlKey - SDLK_1);
    }

    // Printable keys at their standard US QWERTY positions
    switch (sdlKey) {
    case SDLK_0:            return SDL_SCANCODE_0;
    case SDLK_ESCAPE:       return SDL_SCANCODE_ESCAPE;
    case SDLK_TAB:          return SDL_SCANCODE_TAB;
    case SDLK_BACKSPACE:    return SDL_SCANCODE_BACKSPACE;
    case SDLK_SPACE:        return SDL_SCANCODE_SPACE;
    case SDLK_MINUS:        return SDL_SCANCODE_MINUS;
    case SDLK_EQUALS:       return SDL_SCANCODE_EQUALS;
    case SDLK_LEFTBRACKET:  return SDL_SCANCODE_LEFTBRACKET;
    case SDLK_RIGHTBRACKET: return SDL_SCANCODE_RIGHTBRACKET;
    case SDLK_BACKSLASH:    return SDL_SCANCODE_BACKSLASH;
    case SDLK_SEMICOLON:    return SDL_SCANCODE_SEMICOLON;
    case SDLK_QUOTE:        return SDL_SCANCODE_APOSTROPHE;
    case SDLK_BACKQUOTE:    return SDL_SCANCODE_GRAVE;
    case SDLK_COMMA:        return SDL_SCANCODE_COMMA;
    case SDLK_PERIOD:       return SDL_SCANCODE_PERIOD;
    case SDLK_SLASH:        return SDL_SCANCODE_SLASH;
    default:                return SDL_SCANCODE_UNKNOWN;
    }
}

bool HotkeyManager::isBindableKey(int qtKey)
{
    switch (qtKey) {
    // Modifiers cannot be the final key of a combo
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
    // Enter clears the binding instead
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_unknown:
        return false;
    default:
        return qtKeyToSdlKeycode(qtKey, 0) != SDLK_UNKNOWN;
    }
}

int HotkeyManager::normalizeEventModifiers(int qtModifiers)
{
#if defined(Q_OS_DARWIN)
    // Qt swaps Command and Control on macOS (Command arrives as
    // ControlModifier and Control as MetaModifier), but SDL reports the
    // physical keys at stream time. Unswap here so stored bindings always
    // refer to the physical keys and match what SDL sees.
    int mods = qtModifiers & ~(Qt::ControlModifier | Qt::MetaModifier);
    if (qtModifiers & Qt::ControlModifier) {
        mods |= Qt::MetaModifier;
    }
    if (qtModifiers & Qt::MetaModifier) {
        mods |= Qt::ControlModifier;
    }
    return mods;
#else
    return qtModifiers;
#endif
}

int HotkeyManager::qtModsToHotkeyModMask(int qtModifiers)
{
    int mask = 0;
    if (qtModifiers & Qt::ControlModifier) {
        mask |= HkModCtrl;
    }
    if (qtModifiers & Qt::AltModifier) {
        mask |= HkModAlt;
    }
    if (qtModifiers & Qt::ShiftModifier) {
        mask |= HkModShift;
    }
    if (qtModifiers & Qt::MetaModifier) {
        mask |= HkModGui;
    }
    return mask;
}

bool HotkeyManager::getBinding(int action, int& qtModifiers, int& qtKey)
{
    if (action < 0 || action >= ActionCount) {
        qtModifiers = 0;
        qtKey = 0;
        return false;
    }

    QSettings settings;
    settings.beginGroup(SER_HOTKEYS_GROUP);

    QString keyName = QString(k_Actions[action].settingsKey) + SER_KEY_SUFFIX;
    QString modsName = QString(k_Actions[action].settingsKey) + SER_MODS_SUFFIX;

    if (settings.contains(keyName)) {
        qtKey = settings.value(keyName).toInt();
        qtModifiers = settings.value(modsName).toInt();
    }
    else {
        qtKey = k_Actions[action].defaultKey;
        qtModifiers = k_Actions[action].defaultMods;
    }

    return qtKey != 0;
}

void HotkeyManager::writeBinding(int action, int qtModifiers, int qtKey)
{
    QSettings settings;
    settings.beginGroup(SER_HOTKEYS_GROUP);
    settings.setValue(QString(k_Actions[action].settingsKey) + SER_KEY_SUFFIX, qtKey);
    settings.setValue(QString(k_Actions[action].settingsKey) + SER_MODS_SUFFIX, qtModifiers);
}

void HotkeyManager::setHotkey(int action, int qtModifiers, int qtKey)
{
    if (action < 0 || action >= ActionCount || qtKey == 0) {
        return;
    }

#if defined(Q_OS_WIN)
    // The Ignore Hotkey cannot use the Win key as a modifier. Our keyboard
    // hook has to swallow that key to keep the client OS from acting on it,
    // so it cannot also hand it to the combo, and letting one be bound would
    // silently break every other Win shortcut. Reject it rather than storing
    // a combo that cannot work.
    if (action == ActionIgnore && (qtModifiers & Qt::MetaModifier)) {
        return;
    }
#endif

    // Store the canonical (unshifted) key so conflict detection and
    // display are consistent regardless of how the key arrived from Qt
    qtKey = normalizeQtKey(qtKey, (qtModifiers & Qt::KeypadModifier) != 0);

    // A combo may only be bound to one action, so steal it from any other
    // action that currently uses it
    for (int i = 0; i < ActionCount; i++) {
        if (i == action) {
            continue;
        }

        int otherMods, otherKey;
        if (getBinding(i, otherMods, otherKey) &&
                otherMods == qtModifiers && otherKey == qtKey) {
            writeBinding(i, 0, 0);
        }
    }

    writeBinding(action, qtModifiers, qtKey);

    m_Revision++;
    emit hotkeysChanged();
}

void HotkeyManager::clearHotkey(int action)
{
    if (action < 0 || action >= ActionCount) {
        return;
    }

    writeBinding(action, 0, 0);

    m_Revision++;
    emit hotkeysChanged();
}

void HotkeyManager::restoreDefaults()
{
    QSettings settings;
    settings.beginGroup(SER_HOTKEYS_GROUP);
    for (int i = 0; i < ActionCount; i++) {
        settings.remove(QString(k_Actions[i].settingsKey) + SER_KEY_SUFFIX);
        settings.remove(QString(k_Actions[i].settingsKey) + SER_MODS_SUFFIX);
    }

    m_Revision++;
    emit hotkeysChanged();
}

int HotkeyManager::revision()
{
    return m_Revision;
}
