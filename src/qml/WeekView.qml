// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One week, with the hours of the day running down the page:
// events that take whole days at the top, the others as tall as they are long.
Item {
    id: view

    property date selectedDate: new Date()
    readonly property int dayCount: 7

    signal dateSelected(date date)
    signal newEventRequested(date start, bool allDay)
    // Asks for the menu that a right click brings up.
    signal menuRequested(date start, bool allDay)
    signal eventActivated(var event)
    signal dayActivated(date date)

    readonly property int firstDayOfWeek: Backend.firstDayOfWeek
    readonly property date rangeStart: new Date(selectedDate.getFullYear(), selectedDate.getMonth(), selectedDate.getDate() - (selectedDate.getDay() - firstDayOfWeek + 7) % 7)
    // The column with the hours, wide enough for the widest of them.
    readonly property real gutter: hourMetrics.width + Kirigami.Units.largeSpacing * 2
    readonly property real dayWidth: (width - gutter) / dayCount
    readonly property real hourHeight: Kirigami.Units.gridUnit * 3
    readonly property real laneHeight: Kirigami.Units.gridUnit + Kirigami.Units.smallSpacing
    readonly property int lineWidth: Math.ceil(Kirigami.Units.smallSpacing / 4)
    // Handing in the revision has this laid out again whenever events change.
    readonly property var allDayLayout: Backend.weekLayout(rangeStart, dayCount, true, 4, Backend.revision)
    property date now: new Date()

    function addDays(date: date, days: int): date {
        return new Date(date.getFullYear(), date.getMonth(), date.getDate() + days)
    }

    function hourText(hour: int, use24Hour: bool): string {
        return Backend.formatTime(new Date(2000, 0, 1, hour), use24Hour)
    }

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    TextMetrics {
        id: hourMetrics
        font: Kirigami.Theme.defaultFont
        text: view.hourText(22, Backend.use24Hour)
    }

    // Moves the line that marks the present time.
    Timer {
        interval: Kirigami.Units.humanMoment * 15
        running: true
        repeat: true
        onTriggered: view.now = new Date()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The days: their names and numbers.
        Item {
            Layout.fillWidth: true
            implicitHeight: Kirigami.Units.gridUnit * 2 + Kirigami.Units.smallSpacing * 2

            Repeater {
                model: view.dayCount

                QQC2.AbstractButton {
                    id: dayHeader

                    required property int index
                    readonly property date date: view.addDays(view.rangeStart, index)
                    readonly property bool isToday: date.toDateString() === view.now.toDateString()

                    x: view.gutter + index * view.dayWidth
                    width: view.dayWidth
                    height: parent.height
                    hoverEnabled: true
                    text: date.toLocaleDateString(Qt.locale(), Locale.LongFormat)
                    onClicked: view.dayActivated(date)

                    background: Rectangle {
                        color: Kirigami.Theme.textColor
                        opacity: dayHeader.hovered || dayHeader.visualFocus ? 0.1 : 0
                    }
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Item {
                            Layout.fillWidth: true
                        }
                        QQC2.Label {
                            text: Qt.locale().standaloneDayName(dayHeader.date.getDay(), Locale.ShortFormat)
                            opacity: 0.75
                        }
                        // Today's number stands in a filled circle, as in the month.
                        Rectangle {
                            implicitWidth: Math.max(dayNumber.implicitWidth + Kirigami.Units.largeSpacing, implicitHeight)
                            implicitHeight: Kirigami.Units.gridUnit + Kirigami.Units.smallSpacing
                            radius: height / 2
                            color: Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.highlightColor, dayHeader.isToday ? 1 : 0)

                            QQC2.Label {
                                id: dayNumber
                                anchors.centerIn: parent
                                text: dayHeader.date.getDate()
                                font.bold: dayHeader.isToday
                                color: dayHeader.isToday ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }

        // Events that take whole days.
        Item {
            id: allDayStrip

            readonly property int lanes: Math.max(1, view.allDayLayout.lanes)

            Layout.fillWidth: true
            implicitHeight: lanes * view.laneHeight + Kirigami.Units.smallSpacing

            QQC2.Label {
                width: view.gutter - Kirigami.Units.largeSpacing
                height: view.laneHeight
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                text: i18nc("@label events that take the whole day; keep it short", "All day")
                opacity: 0.75
                elide: Text.ElideRight
            }

            Repeater {
                model: view.dayCount

                Item {
                    id: allDayCell

                    required property int index
                    readonly property date date: view.addDays(view.rangeStart, index)
                    readonly property int more: view.allDayLayout.more[index] ?? 0

                    x: view.gutter + index * view.dayWidth
                    width: view.dayWidth
                    height: allDayStrip.height

                    Kirigami.Separator {
                        anchors {
                            left: parent.left
                            top: parent.top
                            bottom: parent.bottom
                        }
                    }
                    TapHandler {
                        onDoubleTapped: view.newEventRequested(allDayCell.date, true)
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: view.menuRequested(allDayCell.date, true)
                    }
                    QQC2.ToolButton {
                        visible: allDayCell.more > 0
                        x: Kirigami.Units.smallSpacing / 2
                        y: (allDayStrip.lanes - 1) * view.laneHeight
                        width: parent.width - Kirigami.Units.smallSpacing
                        height: view.laneHeight - Kirigami.Units.smallSpacing / 2
                        text: i18ncp("@action:button events that do not fit; shows the day", "%1 More", "%1 More", allDayCell.more)
                        onClicked: view.dayActivated(allDayCell.date)
                    }
                }
            }

            Repeater {
                model: view.allDayLayout.segments

                EventChip {
                    required property var modelData

                    event: modelData
                    x: view.gutter + modelData.column * view.dayWidth + Kirigami.Units.smallSpacing / 2 + view.lineWidth
                    y: modelData.lane * view.laneHeight
                    width: modelData.span * view.dayWidth - Kirigami.Units.smallSpacing - view.lineWidth
                    height: view.laneHeight - Kirigami.Units.smallSpacing / 2
                    onClicked: view.eventActivated(modelData)
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // The hours of the day.
        QQC2.ScrollView {
            id: scroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            contentHeight: view.hourHeight * 24

            // Opens on the working day rather than on the small hours.
            Component.onCompleted: {
                const flickable = contentItem
                const hour = Math.max(0, Math.min(view.now.getHours() - 1, 7))
                flickable.contentY = Math.min(hour * view.hourHeight, Math.max(0, contentHeight - height))
            }

            Item {
                width: scroll.availableWidth
                height: view.hourHeight * 24

                Repeater {
                    model: 24

                    Item {
                        required property int index

                        y: index * view.hourHeight
                        width: parent.width
                        height: view.hourHeight

                        Kirigami.Separator {
                            visible: parent.index > 0
                            x: view.gutter
                            width: parent.width - view.gutter
                        }
                        QQC2.Label {
                            visible: parent.index > 0
                            width: view.gutter - Kirigami.Units.largeSpacing
                            y: -height / 2
                            horizontalAlignment: Text.AlignRight
                            text: view.hourText(parent.index, Backend.use24Hour)
                            opacity: 0.75
                        }
                    }
                }

                Repeater {
                    model: view.dayCount

                    Item {
                        id: dayColumn

                        required property int index
                        readonly property date date: view.addDays(view.rangeStart, index)
                        readonly property bool isToday: date.toDateString() === view.now.toDateString()
                        readonly property var blocks: Backend.dayLayout(date, Backend.revision)

                        x: view.gutter + index * view.dayWidth
                        width: view.dayWidth
                        height: parent.height

                        Kirigami.Separator {
                            anchors {
                                left: parent.left
                                top: parent.top
                                bottom: parent.bottom
                            }
                        }

                        // The time at a height in the column, to the half hour.
                        function timeAt(y: real): date {
                            const halfHours = Math.max(0, Math.min(47, Math.floor(y / (view.hourHeight / 2))))
                            return new Date(date.getFullYear(), date.getMonth(), date.getDate(), Math.floor(halfHours / 2), halfHours % 2 * 30)
                        }

                        // A double click makes an event at that time.
                        TapHandler {
                            onDoubleTapped: (point, button) => view.newEventRequested(dayColumn.timeAt(point.position.y), false)
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: (point, button) => view.menuRequested(dayColumn.timeAt(point.position.y), false)
                        }

                        Repeater {
                            model: dayColumn.blocks

                            EventChip {
                                required property var modelData
                                // A little room at the trailing edge stays free, to double-click on.
                                readonly property real room: dayColumn.width - Kirigami.Units.largeSpacing

                                event: modelData
                                block: true
                                x: view.lineWidth + modelData.column * room / modelData.columns
                                y: modelData.startMinute / 60 * view.hourHeight + view.lineWidth
                                width: room / modelData.columns - view.lineWidth
                                height: (modelData.endMinute - modelData.startMinute) / 60 * view.hourHeight - view.lineWidth
                                onClicked: view.eventActivated(modelData)
                            }
                        }

                        // The present time: a line with a dot at its start.
                        Rectangle {
                            visible: dayColumn.isToday
                            y: (view.now.getHours() * 60 + view.now.getMinutes()) / 60 * view.hourHeight
                            width: parent.width
                            height: Math.ceil(Kirigami.Units.smallSpacing / 2)
                            color: Kirigami.Theme.highlightColor

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.horizontalCenter: parent.left
                                width: Kirigami.Units.largeSpacing
                                height: width
                                radius: height / 2
                                color: Kirigami.Theme.highlightColor
                            }
                        }
                    }
                }
            }
        }
    }
}
