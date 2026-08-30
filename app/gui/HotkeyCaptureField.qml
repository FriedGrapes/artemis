import QtQuick 2.9
import QtQuick.Controls 2.2

import HotkeyManager 1.0

// A field that displays a hotkey binding and captures a new key combination
// when clicked. While capturing:
// - The combo builds as modifier keys (Ctrl/Alt/Shift/Win) are held and
//   completes when one non-modifier key is pressed
// - The combo is committed once every key involved has been released
// - Enter clears the binding
// - Clicking elsewhere (losing focus) cancels and keeps the old binding
TextField {
    id: hotkeyField

    // Index of the HotkeyManager action this field edits
    property int actionIndex: 0

    // True while actively listening for a new key combination
    property bool capturing: false

    // Qt modifier mask of the modifier keys currently held during capture
    property int heldModifierMask: 0

    // Snapshot of the combo being built, set when a non-modifier key is
    // pressed while at least one modifier is held
    property int pendingModifiers: 0
    property int pendingKey: 0

    // Qt key codes pressed during this capture session that haven't been
    // released yet. The combo commits when this empties with a pending combo.
    property var heldKeys: []

    readOnly: true
    font.pointSize: 12

    // Only offer the placeholder while empty so styles with floating
    // placeholder labels don't show it above a bound combo
    placeholderText: text === "" ? qsTr("Press key combination or Enter to clear") : ""

    Component.onCompleted: refresh()

    function refresh() {
        if (!capturing) {
            // Shows the placeholder text when unbound
            text = HotkeyManager.getDisplayString(actionIndex)
        }
        else if (pendingKey !== 0) {
            text = HotkeyManager.formatCombo(pendingModifiers, pendingKey)
        }
        else if (heldModifierMask !== 0) {
            text = HotkeyManager.formatCombo(heldModifierMask, 0)
        }
        else {
            text = qsTr("Press key combination or press enter to clear")
        }
    }

    function resetBuildState() {
        heldModifierMask = 0
        pendingModifiers = 0
        pendingKey = 0
        heldKeys = []
    }

    function startCapture() {
        capturing = true
        resetBuildState()
        refresh()
    }

    function endCapture() {
        capturing = false
        resetBuildState()
        refresh()
        focus = false
    }

    function modifierBitForKey(key) {
        switch (key) {
        case Qt.Key_Control:
            return Qt.ControlModifier
        case Qt.Key_Alt:
            return Qt.AltModifier
        case Qt.Key_Shift:
            return Qt.ShiftModifier
        case Qt.Key_Meta:
        case Qt.Key_Super_L:
        case Qt.Key_Super_R:
            return Qt.MetaModifier
        }
        return 0
    }

    function trackKey(key) {
        var keys = heldKeys
        if (keys.indexOf(key) < 0) {
            keys.push(key)
            heldKeys = keys
        }
    }

    function untrackKey(key) {
        var keys = heldKeys
        var idx = keys.indexOf(key)
        if (idx >= 0) {
            keys.splice(idx, 1)
            heldKeys = keys
        }
    }

    onActiveFocusChanged: {
        if (!activeFocus && capturing) {
            // Clicking or focusing away cancels the capture, leaving the
            // previous binding untouched
            capturing = false
            resetBuildState()
            refresh()
        }
    }

    Keys.onPressed: function(event) {
        if (!capturing) {
            // Not capturing: let keys behave normally (e.g. Tab navigation)
            return
        }

        event.accepted = true

        if (event.isAutoRepeat) {
            return
        }

        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            // Enter always clears the binding, even mid-entry
            HotkeyManager.clearHotkey(actionIndex)
            endCapture()
            return
        }

        var modBit = modifierBitForKey(event.key)
        if (modBit !== 0) {
            heldModifierMask |= modBit
            trackKey(event.key)
            refresh()
        }
        else {
            // Take the modifier state from the event itself so modifiers
            // that were already held before capture started still count
            var mods = event.modifiers & (Qt.ControlModifier | Qt.AltModifier |
                                          Qt.ShiftModifier | Qt.MetaModifier)
            if (mods !== 0 && HotkeyManager.isBindableKey(event.key)) {
                // Snapshot the combo: modifiers held right now plus this key
                pendingKey = event.key
                pendingModifiers = mods | (event.modifiers & Qt.KeypadModifier)
                trackKey(event.key)
                refresh()
            }
            // Non-modifier keys pressed without any modifier held (or keys
            // we can't bind) are ignored
        }
    }

    Keys.onReleased: function(event) {
        if (!capturing) {
            return
        }

        event.accepted = true

        if (event.isAutoRepeat) {
            return
        }

        var modBit = modifierBitForKey(event.key)
        if (modBit !== 0) {
            heldModifierMask &= ~modBit
        }
        untrackKey(event.key)

        if (heldKeys.length === 0) {
            if (pendingKey !== 0) {
                // Everything released with a complete combo: commit it
                HotkeyManager.setHotkey(actionIndex, pendingModifiers, pendingKey)
                endCapture()
            }
            else {
                // Only modifiers were pressed and released: start over
                resetBuildState()
                refresh()
            }
        }
        else {
            refresh()
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
            hotkeyField.forceActiveFocus(Qt.MouseFocusReason)
            hotkeyField.startCapture()
        }
    }

    Connections {
        target: HotkeyManager

        function onHotkeysChanged() {
            // Refresh non-capturing fields so a stolen binding clears
            // immediately in the UI
            if (!hotkeyField.capturing) {
                hotkeyField.refresh()
            }
        }
    }
}
