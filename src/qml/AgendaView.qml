// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The selected day and those after it, one below the other, each with its
// events stacked beside it: no hours down the side, so that a day shows at
// a glance what is on, however late it is.
Item {
    id: view

    property date selectedDate: new Date()
    // How many days there are.
    property int dayCount: 3

    signal newEventRequested(date date)
    // Asks for the menu that a right click brings up.
    signal menuRequested(date date)
    signal eventActivated(var event)

    // The column with the days, wide enough for the widest name.
    readonly property real gutter: nameMetrics.width + Kirigami.Units.gridUnit * 2 + Kirigami.Units.largeSpacing * 3
    readonly property real chipHeight: Kirigami.Units.gridUnit * 2
    property date now: new Date()

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Accessible.role: Accessible.List
    Accessible.name: i18nc("@info accessible name of the agenda", "Agenda")

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    TextMetrics {
        id: nameMetrics

        font: Kirigami.Theme.defaultFont
        // The widest of the seven names.
        text: {
            let widest = ""
            for (let day = 0; day < 7; ++day) {
                const name = Qt.locale().standaloneDayName(day, Locale.ShortFormat)
                if (name.length > widest.length) {
                    widest = name
                }
            }
            return widest
        }
    }

    // Keeps today's mark on the right day.
    Timer {
        interval: Kirigami.Units.humanMoment * 15
        running: true
        repeat: true
        onTriggered: view.now = new Date()
    }

    // Scrolls only if the days have more events than fit.
    QQC2.ScrollView {
        id: scroll

        anchors.fill: parent
        contentWidth: availableWidth

        ListView {
            id: list

            clip: true
            model: view.dayCount
            boundsBehavior: Flickable.StopAtBounds
            // All of the few days at once, so that the list knows how tall it is.
            cacheBuffer: 100000

            delegate: Item {
                id: day

                required property int index
                readonly property date date: new Date(view.selectedDate.getFullYear(), view.selectedDate.getMonth(), view.selectedDate.getDate() + index)
                readonly property bool isToday: date.toDateString() === view.now.toDateString()
                // Handing in the revision has this read again whenever events change.
                // Whole days come first, then the others by their time.
                readonly property var events: Backend.weekLayout(date, 1, false, 0, Backend.revision).segments

                width: ListView.view.width
                height: content.implicitHeight

                Accessible.role: Accessible.ListItem
                Accessible.name: events.length === 0
                    ? i18nc("@info accessible name of a day without events, %1 is a date", "%1, no events",
                        date.toLocaleDateString(Qt.locale(), Locale.LongFormat))
                    : i18ncp("@info accessible name of a day, %2 is a date", "%2, %1 event", "%2, %1 events", events.length,
                        date.toLocaleDateString(Qt.locale(), Locale.LongFormat))

                TapHandler {
                    onDoubleTapped: view.newEventRequested(day.date)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: view.menuRequested(day.date)
                }

                ColumnLayout {
                    id: content

                    width: parent.width
                    spacing: 0

                    Item {
                        Layout.fillWidth: true
                        implicitHeight: Math.max(label.height, chips.implicitHeight) + Kirigami.Units.smallSpacing * 2

                        // The name of the day and its number; today's stands in a
                        // filled circle, as in the month.
                        RowLayout {
                            id: label

                            x: Kirigami.Units.largeSpacing
                            y: Kirigami.Units.smallSpacing
                            width: view.gutter - Kirigami.Units.largeSpacing * 2
                            // Beside the first event, and as tall without one.
                            height: Math.max(implicitHeight, view.chipHeight)
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignRight
                                text: Qt.locale().standaloneDayName(day.date.getDay(), Locale.ShortFormat)
                                opacity: 0.75
                            }
                            Rectangle {
                                implicitWidth: Math.max(number.implicitWidth + Kirigami.Units.largeSpacing, implicitHeight)
                                implicitHeight: Kirigami.Units.gridUnit + Kirigami.Units.smallSpacing
                                radius: height / 2
                                color: Kirigami.ColorUtils.tintWithAlpha("transparent", Kirigami.Theme.highlightColor, day.isToday ? 1 : 0)

                                QQC2.Label {
                                    id: number

                                    anchors.centerIn: parent
                                    text: day.date.getDate()
                                    font.bold: day.isToday
                                    color: day.isToday ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                }
                            }
                        }

                        ColumnLayout {
                            id: chips

                            x: view.gutter
                            y: Kirigami.Units.smallSpacing
                            // A little room at the trailing edge stays free, to click on.
                            width: parent.width - view.gutter - Kirigami.Units.gridUnit
                            spacing: Kirigami.Units.smallSpacing

                            Repeater {
                                model: day.events

                                EventChip {
                                    required property var modelData

                                    Layout.fillWidth: true
                                    implicitHeight: view.chipHeight
                                    event: modelData
                                    row: true
                                    onClicked: view.eventActivated(modelData)
                                }
                            }
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
}
