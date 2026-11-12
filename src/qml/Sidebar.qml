// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A small month to jump to a day, and the calendars with a tick each to
// show or hide their events. Accounts are added in the settings.
Kirigami.GlobalDrawer {
    id: drawer

    property date selectedDate: new Date()

    signal dateChosen(date date)

    // Put away by the user, to have all the room for the calendar.
    property bool collapsed: false

    // Beside the calendar while there is room and it is not put away, and
    // opened on demand otherwise.
    readonly property bool roomy: !Kirigami.Settings.isMobile && applicationWindow().width >= Kirigami.Units.gridUnit * 44
    modal: !roomy
    onModalChanged: drawerOpen = !modal && !collapsed
    onCollapsedChanged: drawerOpen = !modal && !collapsed
    // The toolbar's own button brings it back, which is to be remembered too.
    onDrawerOpenChanged: {
        if (!modal) {
            collapsed = !drawerOpen
        }
    }
    Component.onCompleted: drawerOpen = !modal && !collapsed
    width: Kirigami.Units.gridUnit * 15
    // The toolbar's button that brings the sidebar back.
    handleClosedIcon.name: "sidebar-expand-left"
    handleOpenIcon.name: "sidebar-collapse-left"
    handleClosedToolTip: i18nc("@info:tooltip", "Show Sidebar")
    handleOpenToolTip: i18nc("@info:tooltip", "Hide Sidebar")
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

    // Slides into view while events are fetched, so that it shows that
    // something is going on, and how far it has come.
    footer: Item {
        id: status

        readonly property bool wanted: Backend.syncing

        implicitHeight: wanted ? bar.implicitHeight : 0
        visible: implicitHeight > 0
        clip: true

        Behavior on implicitHeight {
            NumberAnimation {
                duration: Kirigami.Units.longDuration
                easing.type: Easing.InOutQuad
            }
        }

        QQC2.ToolBar {
            id: bar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            position: QQC2.ToolBar.Footer

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18nc("@info:progress", "Fetching events…")
                    elide: Text.ElideRight
                }
                QQC2.ProgressBar {
                    Layout.fillWidth: true
                    indeterminate: Backend.syncProgress < 0
                    value: Math.max(0, Backend.syncProgress)
                }
            }
        }
    }
}
