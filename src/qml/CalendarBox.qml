// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Chooses the calendar that events go to. Each is shown with its color and
// where it is kept, as calendars of different accounts often have the same
// name.
RowLayout {
    id: row

    property string calendarId
    readonly property alias count: box.count
    // What FormData.buddyFor is to name.
    readonly property alias box: box

    signal chosen(string calendarId)

    spacing: Kirigami.Units.smallSpacing

    Rectangle {
        implicitWidth: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
        implicitHeight: implicitWidth
        radius: height / 2
        color: box.current?.color ?? "transparent"
    }
    QQC2.ComboBox {
        id: box

        readonly property var current: model[currentIndex] ?? null

        Layout.fillWidth: true
        textRole: "name"
        valueRole: "id"
        model: Backend.writableCalendars
        currentIndex: Math.max(0, indexOfValue(row.calendarId))
        displayText: current ? i18nc("@item:inlistbox %1 is the name of a calendar, %2 where it is kept, such as “Local”",
            "%1 (%2)", current.name, current.source) : ""
        onActivated: row.chosen(currentValue)

        delegate: QQC2.ItemDelegate {
            id: choice

            required property var modelData
            required property int index

            width: parent?.width ?? 0
            text: modelData.name
            highlighted: box.highlightedIndex === index
            Accessible.description: modelData.source

            contentItem: CalendarLabel {
                name: choice.modelData.name
                source: choice.modelData.source
                calendarColor: choice.modelData.color
                highlighted: choice.highlighted
            }
        }
    }
}
