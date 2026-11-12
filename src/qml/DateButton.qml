// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// Shows a day and lets another be picked from a small month.
QQC2.Button {
    id: button

    property date date: new Date()

    signal dateChosen(date date)

    text: date.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    Accessible.name: date.toLocaleDateString(Qt.locale(), Locale.LongFormat)
    icon.name: "view-calendar"
    checkable: true
    checked: picker.visible
    onClicked: picker.visible ? picker.close() : picker.open()

    QQC2.Popup {
        id: picker

        y: button.height
        padding: Kirigami.Units.largeSpacing
        closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutsideParent

        contentItem: MiniMonth {
            selectedDate: button.date
            onDateChosen: date => {
                picker.close()
                button.dateChosen(date)
            }
        }
    }
}
