#pragma once

#include <QObject>

// Manages user-configurable hotkey bindings for in-stream actions.
//
// Bindings are captured in the settings UI using Qt key codes and modifier
// masks, persisted via QSettings, and converted to SDL keycodes/scancodes
// when a streaming session starts (see SdlInputHandler).
class HotkeyManager : public QObject
{
    Q_OBJECT

public:
    // NOTE: This enum must stay in the same order as SdlInputHandler::KeyCombo.
    // New entries must be added immediately before ActionCount.
    enum HotkeyAction
    {
        ActionQuit,
        ActionUngrabInput,
        ActionToggleFullScreen,
        ActionToggleStatsOverlay,
        ActionToggleMouseMode,
        ActionToggleCursorHide,
        ActionToggleMinimize,
        ActionPasteText,
        ActionTogglePointerRegionLock,
        ActionQuitAndExit,
        ActionToggleQuickMenu,
        ActionIgnore,
        ActionCount
    };
    Q_ENUM(HotkeyAction)

    // Modifier bits used for hotkey matching at stream time. These are
    // deliberately independent of both Qt and SDL modifier masks.
    enum HotkeyModifier
    {
        HkModCtrl  = 0x1,
        HkModAlt   = 0x2,
        HkModShift = 0x4,
        HkModGui   = 0x8,
    };

    static HotkeyManager* get();

    // Number of configurable hotkey actions
    Q_INVOKABLE int getActionCount();

    // Translated display name for an action
    Q_INVOKABLE QString getActionName(int action);

    // Translated description for an action (used for tooltips)
    Q_INVOKABLE QString getActionDescription(int action);

    // Display string of the action's current binding ("" if unbound)
    Q_INVOKABLE QString getDisplayString(int action);

    // Formats an in-progress or complete combo for display. A qtKey of 0
    // renders just the modifiers with a trailing '+' to indicate that the
    // combo is incomplete.
    Q_INVOKABLE QString formatCombo(int qtModifiers, int qtKey);

    // Returns true if the given Qt key can be used as the final key of a
    // combo (not a modifier, not Enter, and mappable to an SDL keycode)
    Q_INVOKABLE bool isBindableKey(int qtKey);

    // Converts modifiers as reported by Qt key events into the physical
    // modifier keys they represent. On macOS, Qt swaps Command and Control
    // but SDL (which matches hotkeys at stream time) does not, so captured
    // modifiers must be unswapped before storage or display. A no-op on
    // other platforms.
    Q_INVOKABLE int normalizeEventModifiers(int qtModifiers);

    // Binds a combo to an action, unbinding it from any other action first
    Q_INVOKABLE void setHotkey(int action, int qtModifiers, int qtKey);

    // Unbinds an action
    Q_INVOKABLE void clearHotkey(int action);

    // Restores all actions to their default bindings
    Q_INVOKABLE void restoreDefaults();

    // Dummy revision property that QML bindings can reference to be
    // re-evaluated whenever any hotkey binding changes
    Q_PROPERTY(int revision READ revision NOTIFY hotkeysChanged)
    int revision();

    // Returns true if the action is bound, filling in the Qt modifier mask
    // (including Qt::KeypadModifier if applicable) and Qt key code.
    // Safe to call without a HotkeyManager instance.
    static bool getBinding(int action, int& qtModifiers, int& qtKey);

    // Converts a Qt modifier mask to a HotkeyModifier mask
    static int qtModsToHotkeyModMask(int qtModifiers);

    // Converts a Qt key code to an SDL keycode (SDLK_UNKNOWN if unmappable).
    // The Qt modifier mask is consulted for Qt::KeypadModifier only.
    static int qtKeyToSdlKeycode(int qtKey, int qtModifiers);

    // Returns the standard US QWERTY scancode for an SDL keycode
    // (SDL_SCANCODE_UNKNOWN if there is no fixed position). Used as a
    // layout-independent fallback for matching hotkeys on non-latin
    // keyboard layouts.
    static int sdlKeycodeToDefaultScancode(int sdlKey);

signals:
    void hotkeysChanged();

private:
    explicit HotkeyManager(QObject* parent = nullptr);

    static void writeBinding(int action, int qtModifiers, int qtKey);

    int m_Revision;
};
