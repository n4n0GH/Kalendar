// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A small month to jump to a day, and the calendars with a tick each to
// show or hide their events.
Kirigami.GlobalDrawer {
    id: drawer

    property date selectedDate: new Date()

    signal dateChosen(date date)
    signal addAccountRequested()

    // Beside the calendar while there is room, and opened on demand otherwise.
    readonly property bool roomy: !Kirigami.Settings.isMobile && applicationWindow().width >= Kirigami.Units.gridUnit * 44
    modal: !roomy
    onModalChanged: drawerOpen = !modal
    width: Kirigami.Units.gridUnit * 15
    collapsible: false
    leftPadding: 0
    rightPadding: 0

    // At the top of the drawer; what is not needed below stays empty.
    topContent: [
        MiniMonth {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            selectedDate: drawer.selectedDate
            onDateChosen: date => drawer.dateChosen(date)
        },
        Kirigami.Separator {
            Layout.fillWidth: true
        },
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Repeater {
                model: Backend.calendars

                ColumnLayout {
                    id: row

                    required property string calendarId
                    required property string name
                    required property color calendarColor
                    required property bool shown
                    required property string accountName
                    required property bool firstOfAccount

                    Layout.fillWidth: true
                    spacing: 0

                    Kirigami.ListSectionHeader {
                        Layout.fillWidth: true
                        visible: row.firstOfAccount
                        text: row.accountName
                    }
                    QQC2.CheckDelegate {
                        id: tick

                        Layout.fillWidth: true
                        text: row.name
                        onToggled: Backend.setCalendarShown(row.calendarId, checked)

                        // Ticking sets "checked" itself; this keeps it following the calendar.
                        Binding {
                            target: tick
                            property: "checked"
                            value: row.shown
                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Rectangle {
                                implicitWidth: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
                                implicitHeight: implicitWidth
                                radius: height / 2
                                color: row.calendarColor
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: tick.text
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }
                        }
                    }
                }
            }
        }
    ]

    footer: QQC2.ToolBar {
        position: QQC2.ToolBar.Footer

        contentItem: RowLayout {
            QQC2.ToolButton {
                Layout.fillWidth: true
                text: i18nc("@action:button", "Add Account…")
                icon.name: "list-add"
                onClicked: drawer.addAccountRequested()
            }
        }
    }
}
