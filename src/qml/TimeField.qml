// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// A time of day: picked from a list of half hours, or typed.
QQC2.ComboBox {
    id: field

    // The time is the time of day of this; its date is kept.
    property date value: new Date()

    signal timeChosen(date time)

    function show(): void {
        editText = Backend.formatTime(value, Backend.use24Hour)
    }

    // Takes over what was typed if it names a time, and puts back the old one if not.
    function commit(text: string): void {
        const time = Backend.parseTime(text, value)
        if (isNaN(time.getTime())) {
            show()
        } else if (time.getTime() !== value.getTime()) {
            timeChosen(time)
        } else {
            show()
        }
    }

    implicitWidth: Kirigami.Units.gridUnit * 7
    editable: true
    model: {
        const times = []
        for (let halfHour = 0; halfHour < 48; halfHour++) {
            times.push(Backend.formatTime(new Date(2000, 0, 1, Math.floor(halfHour / 2), halfHour % 2 * 30), Backend.use24Hour))
        }
        return times
    }

    onValueChanged: show()
    onModelChanged: show()
    Component.onCompleted: show()
    onActivated: index => commit(textAt(index))
    onAccepted: commit(editText)
    onActiveFocusChanged: {
        if (!activeFocus) {
            commit(editText)
        }
    }
}
