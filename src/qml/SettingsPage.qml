// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigami.delegates as KD

// What can be set up: the accounts calendars come from, how the calendar
// looks, and whether other calendar programs get to show the events.
Kirigami.ScrollablePage {
    id: page

    // Why Kalendar's events could not be handed to the system, if so.
    readonly property string errorText: SystemExport.problem

    // What is chosen here, which only counts once "Apply" is clicked.
    property bool use24Hour: Backend.use24Hour
    property int firstDayOfWeek: Backend.firstDayOfWeek
    property bool showWeekNumbers: Backend.showWeekNumbers
    property int agendaDays: applicationWindow().agendaDays
    readonly property bool changed: use24Hour !== Backend.use24Hour || firstDayOfWeek !== Backend.firstDayOfWeek
        || showWeekNumbers !== Backend.showWeekNumbers || agendaDays !== applicationWindow().agendaDays

    function apply(): void {
        Backend.use24Hour = use24Hour
        Backend.firstDayOfWeek = firstDayOfWeek
        Backend.showWeekNumbers = showWeekNumbers
        applicationWindow().agendaDays = agendaDays
    }

    title: i18nc("@title", "Settings")
    // The calendar may have been removed in another program meanwhile.
    Component.onCompleted: SystemExport.refresh()

    // Only for what went wrong; what went well is said in passing.
    header: Kirigami.InlineMessage {
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Error
        visible: page.errorText.length > 0
        text: page.errorText
    }

    // As in System Settings: choices are tried out above and only kept by
    // "Apply". Accounts and what the system is handed are things done, and done at once.
    footer: ColumnLayout {
        spacing: 0

        // Sets the buttons apart from what scrolls above them.
        Kirigami.Separator {
            Layout.fillWidth: true
        }
        QQC2.DialogButtonBox {
            Layout.fillWidth: true
            standardButtons: QQC2.DialogButtonBox.Apply | QQC2.DialogButtonBox.Cancel
            onApplied: page.apply()
            // Forgets what was chosen and goes back to the calendar.
            onRejected: applicationWindow().pageStack.layers.pop()
            Component.onCompleted: standardButton(QQC2.DialogButtonBox.Apply).enabled = Qt.binding(() => page.changed)
        }
    }

    // Each question has its own choices; without these, all of them would be one.
    QQC2.ButtonGroup {
        id: hoursGroup
    }
    QQC2.ButtonGroup {
        id: agendaGroup
    }
    QQC2.ButtonGroup {
        id: weekGroup
    }

    Kirigami.PromptDialog {
        id: removeDialog

        property var account: null

        title: i18nc("@title:window", "Remove Account")
        subtitle: account ? i18nc("@info %1 is the name of an account",
            "“%1” and its calendars will be removed from Kalendar. The account itself and its events on the server are not touched.", account.name) : ""
        dialogType: Kirigami.PromptDialog.Warning
        standardButtons: Kirigami.Dialog.Cancel

        customFooterActions: Kirigami.Action {
            text: i18nc("@action:button", "Remove")
            icon.name: "edit-delete-remove"
            onTriggered: {
                Backend.removeAccount(removeDialog.account.id)
                removeDialog.close()
            }
        }
    }

    Kirigami.FormLayout {
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Accounts")
        }

        // Where calendars come from: each account, and a way to add one.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: Kirigami.Units.gridUnit * 20
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: Backend.accounts

                RowLayout {
                    id: row

                    required property var modelData
                    readonly property bool failed: modelData.error.length > 0

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    KD.IconTitleSubtitle {
                        Layout.fillWidth: true
                        title: row.modelData.name
                        // A problem is told in words and with a warning sign, not by colour.
                        subtitle: row.failed ? row.modelData.error : row.modelData.description
                        icon.name: row.failed ? "data-warning"
                            : row.modelData.kind === "local" ? "computer"
                            : row.modelData.kind === "system" ? "view-calendar-list" : "folder-cloud"
                        wrapMode: Text.WordWrap
                    }
                    QQC2.ToolButton {
                        visible: row.modelData.removable
                        text: i18nc("@action:button", "Remove Account…")
                        icon.name: "edit-delete-remove"
                        display: QQC2.AbstractButton.IconOnly
                        onClicked: {
                            removeDialog.account = row.modelData
                            removeDialog.open()
                        }

                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered || pressed
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    }
                }
            }
            QQC2.Button {
                text: i18nc("@action:button", "Add Account…")
                icon.name: "list-add"
                onClicked: applicationWindow().addAccount()
            }
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Calendar")
        }

        QQC2.RadioButton {
            Kirigami.FormData.label: i18nc("@label how times of day are written", "Times of day:")
            text: i18nc("@option:radio %1 is an example, such as “2:30 PM”", "With AM and PM (%1)", Backend.formatTime(new Date(2000, 0, 1, 14, 30), false))
            checked: !page.use24Hour
            QQC2.ButtonGroup.group: hoursGroup
            onToggled: page.use24Hour = false
        }
        QQC2.RadioButton {
            text: i18nc("@option:radio %1 is an example, such as “14:30”", "With 24 hours (%1)", Backend.formatTime(new Date(2000, 0, 1, 14, 30), true))
            checked: page.use24Hour
            QQC2.ButtonGroup.group: hoursGroup
            onToggled: page.use24Hour = true
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
        }

        QQC2.RadioButton {
            Kirigami.FormData.label: i18nc("@label the day weeks start on", "Weeks start on:")
            text: Qt.locale().standaloneDayName(0, Locale.LongFormat)
            checked: page.firstDayOfWeek === 0
            QQC2.ButtonGroup.group: weekGroup
            onToggled: page.firstDayOfWeek = 0
        }
        QQC2.RadioButton {
            text: Qt.locale().standaloneDayName(1, Locale.LongFormat)
            checked: page.firstDayOfWeek === 1
            QQC2.ButtonGroup.group: weekGroup
            onToggled: page.firstDayOfWeek = 1
        }
        QQC2.CheckBox {
            Kirigami.FormData.label: i18nc("@label what the small month in the sidebar shows", "Month in the sidebar:")
            text: i18nc("@option:check", "Number the weeks")
            checked: page.showWeekNumbers
            onToggled: page.showWeekNumbers = checked
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
        }

        QQC2.RadioButton {
            Kirigami.FormData.label: i18nc("@label how many days the agenda view shows", "Agenda shows:")
            text: i18nc("@option:radio", "The next 3 days")
            checked: page.agendaDays !== 7
            QQC2.ButtonGroup.group: agendaGroup
            onToggled: page.agendaDays = 3
        }
        QQC2.RadioButton {
            text: i18nc("@option:radio", "The next 7 days")
            checked: page.agendaDays === 7
            QQC2.ButtonGroup.group: agendaGroup
            onToggled: page.agendaDays = 7
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18nc("@title:group", "Other Calendar Programs")
        }

        QQC2.Label {
            Kirigami.FormData.label: i18nc("@label what is handed to the other calendar programs of the system", "Events of Kalendar:")
            visible: !SystemExport.available
            text: i18nc("@info Akonadi is the name of the service that keeps the calendars of KDE’s other programs",
                "There is no Akonadi on this system to hand them to.")
        }
        QQC2.Button {
            Kirigami.FormData.label: i18nc("@label what is handed to the other calendar programs of the system", "Events of Kalendar:")
            visible: SystemExport.available
            enabled: !SystemExport.busy
            text: SystemExport.shown ? i18nc("@action:button", "Remove from the System’s Calendars")
                : i18nc("@action:button", "Add to the System’s Calendars")
            icon.name: SystemExport.shown ? "edit-delete-remove" : "list-add"
            onClicked: {
                if (SystemExport.shown) {
                    SystemExport.remove()
                } else {
                    SystemExport.add()
                }
            }
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            text: SystemExport.shown
                ? i18nc("@info", "The events of the calendars that are shown here are among the system’s calendars, as a calendar named “Kalendar” that can only be looked at. KOrganizer and Merkuro show it, and so does the calendar that opens from the clock in the panel, once “PIM Events Plugin” is switched on in the clock’s settings, under “Calendar”.")
                : i18nc("@info", "Hands the events of the calendars that are shown here to Akonadi, from which KOrganizer, Merkuro and the calendar that opens from the clock in the panel take what they show. There they can be looked at; they are changed here.")
            wrapMode: Text.WordWrap
            opacity: 0.75
        }
    }
}
