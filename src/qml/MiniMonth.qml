// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One small month to pick a day from.
ColumnLayout {
    id: mini

    property date selectedDate: new Date()
    // Any day of the month that is shown; follows the selected day.
    property date shownMonth: selectedDate

    signal dateChosen(date date)

    readonly property int firstDayOfWeek: Qt.locale().firstDayOfWeek
    readonly property date gridStart: {
        const first = new Date(shownMonth.getFullYear(), shownMonth.getMonth(), 1)
        return new Date(first.getFullYear(), first.getMonth(), 1 - (first.getDay() - firstDayOfWeek + 7) % 7)
    }
    readonly property real cellSize: Kirigami.Units.gridUnit + Kirigami.Units.largeSpacing

    function showMonth(offset: int): void {
        shownMonth = new Date(shownMonth.getFullYear(), shownMonth.getMonth() + offset, 1)
    }

    onSelectedDateChanged: shownMonth = selectedDate
    spacing: Kirigami.Units.smallSpacing

    RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            level: 4
            text: mini.shownMonth.toLocaleDateString(Qt.locale(), "MMMM yyyy")
            elide: Text.ElideRight
        }
        QQC2.ToolButton {
            text: i18nc("@action:button", "Show Previous Month")
            icon.name: "go-up"
            display: QQC2.AbstractButton.IconOnly
            onClicked: mini.showMonth(-1)

            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered || pressed
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
        QQC2.ToolButton {
            text: i18nc("@action:button", "Show Next Month")
            icon.name: "go-down"
            display: QQC2.AbstractButton.IconOnly
            onClicked: mini.showMonth(1)

            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered || pressed
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }
    }

    Grid {
        Layout.alignment: Qt.AlignHCenter
        columns: 7

        Repeater {
            model: 7

            QQC2.Label {
                required property int index

                width: mini.cellSize
                horizontalAlignment: Text.AlignHCenter
                text: Qt.locale().standaloneDayName((mini.firstDayOfWeek + index) % 7, Locale.NarrowFormat)
                opacity: 0.75
                Accessible.ignored: true
            }
        }

        Repeater {
            model: 42

            QQC2.AbstractButton {
                id: day

                required property int index
                readonly property date date: new Date(mini.gridStart.getFullYear(), mini.gridStart.getMonth(), mini.gridStart.getDate() + index)
                readonly property bool isSelected: date.toDateString() === mini.selectedDate.toDateString()
                readonly property bool isToday: date.toDateString() === new Date().toDateString()
                readonly property bool inMonth: date.getMonth() === mini.shownMonth.getMonth()

                width: mini.cellSize
                height: mini.cellSize
                hoverEnabled: true
                text: date.getDate()
                Accessible.name: date.toLocaleDateString(Qt.locale(), Locale.LongFormat)
                onClicked: mini.dateChosen(date)

                background: Rectangle {
                    radius: height / 2
                    color: day.isSelected ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                    opacity: day.isSelected ? 1 : (day.hovered || day.visualFocus ? 0.15 : 0)
                }
                contentItem: QQC2.Label {
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: day.text
                    // Today is marked twice over: bold and in the accent colour.
                    font.bold: day.isToday
                    color: day.isSelected ? Kirigami.Theme.highlightedTextColor
                        : day.isToday ? Kirigami.Theme.activeTextColor
                        : day.inMonth ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                }
            }
        }
    }
}
