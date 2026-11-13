// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One event in a view. Four looks: a bar for events that take whole days,
// a line with a dot for events at a time of day in the month view, a
// block as tall as the event is long in the week view, and a row with the
// title at one end and the time at the other in the list of days.
QQC2.AbstractButton {
    id: chip

    required property var event
    property bool block: false
    property bool row: false

    readonly property bool filled: block || row || event.bar
    // When it is, for the row: the times, or that it takes the whole day.
    readonly property string rangeText: {
        if (event.allDay) {
            return i18nc("@label events that take the whole day; keep it short", "All day")
        }
        const sameDay = event.start.toDateString() === event.end.toDateString()
        if (sameDay && event.start.getTime() === event.end.getTime()) {
            return timeText
        }
        const end = Backend.formatTime(event.end, Backend.use24Hour)
        return i18nc("@info a range of times", "%1 – %2", sameDay ? timeText
            : i18nc("@info a day and a time of day", "%1 %2", event.start.toLocaleDateString(Qt.locale(), Locale.ShortFormat), timeText),
            sameDay ? end : i18nc("@info a day and a time of day", "%1 %2", event.end.toLocaleDateString(Qt.locale(), Locale.ShortFormat), end))
    }
    readonly property string timeText: event.allDay ? "" : Backend.formatTime(event.start, Backend.use24Hour)
    readonly property string description: {
        const locale = Qt.locale()
        let when
        if (event.allDay) {
            when = event.start.toDateString() === event.end.toDateString()
                ? event.start.toLocaleDateString(locale, Locale.LongFormat)
                : i18nc("@info a range of days", "%1 – %2", event.start.toLocaleDateString(locale, Locale.ShortFormat),
                    event.end.toLocaleDateString(locale, Locale.ShortFormat))
        } else {
            const moment = time => i18nc("@info a day and a time of day", "%1 %2", time.toLocaleDateString(locale, Locale.ShortFormat),
                Backend.formatTime(time, Backend.use24Hour))
            when = i18nc("@info a range of times", "%1 – %2", moment(event.start),
                event.start.toDateString() === event.end.toDateString() ? Backend.formatTime(event.end, Backend.use24Hour) : moment(event.end))
        }
        return event.location.length > 0 ? i18nc("@info time and place of an event", "%1, %2", when, event.location) : when
    }

    text: event.title
    hoverEnabled: true
    leftPadding: Kirigami.Units.smallSpacing
    rightPadding: Kirigami.Units.smallSpacing
    topPadding: block ? Kirigami.Units.smallSpacing / 2 : 0
    bottomPadding: 0

    Accessible.role: Accessible.Button
    Accessible.name: i18nc("@info title, then time and place of an event", "%1, %2", event.title, description)

    QQC2.ToolTip.text: Accessible.name
    QQC2.ToolTip.visible: hovered || pressed
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    background: Rectangle {
        radius: Kirigami.Units.cornerRadius
        // The calendar's colour is mixed into the background, so that the
        // text on it stays readable in light and in dark colour schemes.
        color: chip.filled ? Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, chip.event.color, chip.hovered ? 0.45 : 0.3)
            : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, chip.hovered || chip.visualFocus ? 0.1 : 0)
        border.width: chip.visualFocus ? Math.ceil(Kirigami.Units.smallSpacing / 4) : 0
        border.color: Kirigami.Theme.focusColor

        // The calendar's colour in full, at the leading edge.
        Rectangle {
            visible: chip.filled
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
            }
            width: Kirigami.Units.smallSpacing
            radius: Kirigami.Units.cornerRadius
            color: chip.event.color
        }
    }

    contentItem: Item {
        clip: true

        // In one line: for the month view and for events that take whole days.
        RowLayout {
            visible: !chip.block && !chip.row
            anchors.fill: parent
            anchors.leftMargin: chip.filled ? Kirigami.Units.smallSpacing : 0
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                visible: !chip.filled
                implicitWidth: Kirigami.Units.largeSpacing
                implicitHeight: Kirigami.Units.largeSpacing
                radius: height / 2
                color: chip.event.color
            }
            QQC2.Label {
                // Where room is short, the title matters more than the time.
                visible: !chip.filled && chip.width >= Kirigami.Units.gridUnit * 8
                text: chip.timeText
                opacity: 0.75
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: chip.event.title
                elide: Text.ElideRight
            }
        }

        // The title at one end, time and place at the other: for the list of days.
        RowLayout {
            visible: chip.row
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.largeSpacing
            anchors.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: chip.event.title
                elide: Text.ElideRight
            }
            QQC2.Label {
                // The title keeps at least half of the row.
                Layout.maximumWidth: chip.width / 2
                text: chip.event.location.length > 0
                    ? i18nc("@info time and place of an event", "%1, %2", chip.rangeText, chip.event.location) : chip.rangeText
                opacity: 0.75
                elide: Text.ElideRight
            }
        }

        // Several lines: for the blocks of the week view.
        ColumnLayout {
            visible: chip.block
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                leftMargin: Kirigami.Units.smallSpacing
            }
            spacing: 0

            QQC2.Label {
                Layout.fillWidth: true
                text: chip.event.title
                font.bold: true
                elide: Text.ElideRight
            }
            QQC2.Label {
                Layout.fillWidth: true
                // Only where the event is long enough for a second line.
                visible: chip.height >= implicitHeight * 2
                text: chip.event.location.length > 0
                    ? i18nc("@info time and place of an event", "%1, %2", chip.timeText, chip.event.location) : chip.timeText
                opacity: 0.75
                elide: Text.ElideRight
            }
        }
    }
}
