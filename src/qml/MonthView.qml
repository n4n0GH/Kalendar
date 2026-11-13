// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A month as six weeks of seven days, each with as many of its events as fit.
FocusScope {
    id: view

    property date selectedDate: new Date()

    signal dateSelected(date date)
    signal stepRequested(int direction)
    signal newEventRequested(date date)
    // Asks for the menu that a right click brings up.
    signal menuRequested(date date)
    signal eventActivated(var event)
    // Asks to show one day in full, because not all its events fit here.
    signal dayActivated(date date)

    readonly property int firstDayOfWeek: Backend.firstDayOfWeek
    readonly property date gridStart: {
        const first = new Date(selectedDate.getFullYear(), selectedDate.getMonth(), 1)
        return new Date(first.getFullYear(), first.getMonth(), 1 - (first.getDay() - firstDayOfWeek + 7) % 7)
    }
    readonly property real cellWidth: width / 7
    // The row with the number of the day, and one row per event below it.
    readonly property real numberHeight: Kirigami.Units.gridUnit + Kirigami.Units.largeSpacing
    readonly property real laneHeight: Kirigami.Units.gridUnit + Kirigami.Units.smallSpacing
    readonly property int lineWidth: Math.ceil(Kirigami.Units.smallSpacing / 4)

    function addDays(date: date, days: int): date {
        return new Date(date.getFullYear(), date.getMonth(), date.getDate() + days)
    }

    activeFocusOnTab: true
    Accessible.role: Accessible.Table
    Accessible.name: i18nc("@info accessible name of the month view", "Month, %1 selected",
        selectedDate.toLocaleDateString(Qt.locale(), Locale.LongFormat))

    Keys.onLeftPressed: dateSelected(addDays(selectedDate, Qt.application.layoutDirection === Qt.RightToLeft ? 1 : -1))
    Keys.onRightPressed: dateSelected(addDays(selectedDate, Qt.application.layoutDirection === Qt.RightToLeft ? -1 : 1))
    Keys.onUpPressed: dateSelected(addDays(selectedDate, -7))
    Keys.onDownPressed: dateSelected(addDays(selectedDate, 7))
    Keys.onReturnPressed: newEventRequested(selectedDate)
    Keys.onEnterPressed: newEventRequested(selectedDate)

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    // Turning the wheel or swiping with two fingers leafs through the months.
    WheelHandler {
        property real travelled: 0

        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: wheel => {
            travelled += wheel.angleDelta.y
            if (Math.abs(travelled) >= 120) {
                view.stepRequested(travelled > 0 ? -1 : 1)
                travelled = 0
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Row {
            Layout.fillWidth: true

            Repeater {
                model: 7

                QQC2.Label {
                    required property int index

                    width: view.cellWidth
                    topPadding: Kirigami.Units.smallSpacing
                    bottomPadding: Kirigami.Units.smallSpacing
                    rightPadding: Kirigami.Units.largeSpacing
                    horizontalAlignment: Text.AlignRight
                    text: Qt.locale().standaloneDayName((view.firstDayOfWeek + index) % 7, Locale.ShortFormat)
                    opacity: 0.75
                    elide: Text.ElideRight
                }
            }
        }

        Repeater {
            model: 6

            Item {
                id: week

                required property int index
                readonly property date weekStart: view.addDays(view.gridStart, index * 7)
                readonly property int maxLanes: Math.max(1, Math.floor((height - view.numberHeight) / view.laneHeight))
                // Handing in the revision has this laid out again whenever events change.
                readonly property var layout: Backend.weekLayout(weekStart, 7, false, maxLanes, Backend.revision)

                Layout.fillWidth: true
                Layout.fillHeight: true
                // All weeks are equally tall, whatever is in them.
                Layout.preferredHeight: view.numberHeight + view.laneHeight

                Kirigami.Separator {
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                    }
                }

                Repeater {
                    model: 7

                    Item {
                        id: cell

                        required property int index
                        readonly property date date: view.addDays(week.weekStart, index)
                        readonly property bool isToday: date.toDateString() === new Date().toDateString()
                        readonly property bool isSelected: date.toDateString() === view.selectedDate.toDateString()
                        readonly property bool inMonth: date.getMonth() === view.selectedDate.getMonth()
                        readonly property int more: week.layout.more[index] ?? 0

                        x: index * view.cellWidth
                        width: view.cellWidth
                        height: week.height

                        // The selected day, and with a frame while the keyboard moves it.
                        Rectangle {
                            anchors.fill: parent
                            anchors.topMargin: view.lineWidth
                            visible: cell.isSelected
                            color: Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, Kirigami.Theme.highlightColor, 0.15)
                            border.width: view.activeFocus ? view.lineWidth : 0
                            border.color: Kirigami.Theme.focusColor
                        }

                        Kirigami.Separator {
                            visible: cell.index > 0
                            anchors {
                                left: parent.left
                                top: parent.top
                                bottom: parent.bottom
                            }
                        }

                        TapHandler {
                            onTapped: {
                                view.forceActiveFocus()
                                view.dateSelected(cell.date)
                            }
                            onDoubleTapped: view.newEventRequested(cell.date)
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                view.forceActiveFocus()
                                view.dateSelected(cell.date)
                                view.menuRequested(cell.date)
                            }
                        }

                        // The number of the day; today's stands in a filled circle.
                        Rectangle {
                            id: marker

                            anchors {
                                right: parent.right
                                top: parent.top
                                rightMargin: Kirigami.Units.smallSpacing
                                topMargin: Kirigami.Units.smallSpacing
                            }
                            width: Math.max(number.implicitWidth + Kirigami.Units.largeSpacing, height)
                            height: view.numberHeight - Kirigami.Units.smallSpacing
                            radius: height / 2
                            color: Kirigami.Theme.highlightColor
                            opacity: cell.isToday ? 1 : 0
                        }
                        QQC2.Label {
                            id: number

                            anchors.centerIn: marker
                            // The first of a month says which month.
                            text: cell.date.getDate() === 1 ? cell.date.toLocaleDateString(Qt.locale(), "d MMM") : cell.date.getDate()
                            font.bold: cell.isToday
                            color: cell.isToday ? Kirigami.Theme.highlightedTextColor
                                : cell.inMonth ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                            Accessible.name: cell.date.toLocaleDateString(Qt.locale(), Locale.LongFormat)
                        }

                        QQC2.ToolButton {
                            visible: cell.more > 0
                            x: Kirigami.Units.smallSpacing / 2
                            y: view.numberHeight + (week.maxLanes - 1) * view.laneHeight
                            width: parent.width - Kirigami.Units.smallSpacing
                            height: view.laneHeight - Kirigami.Units.smallSpacing / 2
                            text: i18ncp("@action:button events that do not fit; shows the day", "%1 More", "%1 More", cell.more)
                            Accessible.name: i18ncp("@action:button", "Show %1 More Event on %2", "Show %1 More Events on %2", cell.more,
                                cell.date.toLocaleDateString(Qt.locale(), Locale.LongFormat))
                            onClicked: view.dayActivated(cell.date)
                        }
                    }
                }

                Repeater {
                    model: week.layout.segments

                    EventChip {
                        required property var modelData

                        event: modelData
                        x: modelData.column * view.cellWidth + Kirigami.Units.smallSpacing / 2 + view.lineWidth
                        y: view.numberHeight + modelData.lane * view.laneHeight
                        width: modelData.span * view.cellWidth - Kirigami.Units.smallSpacing - view.lineWidth
                        height: view.laneHeight - Kirigami.Units.smallSpacing / 2
                        onClicked: view.eventActivated(modelData)
                    }
                }
            }
        }
    }
}
