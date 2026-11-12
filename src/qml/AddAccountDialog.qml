// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Adds the calendars of a Google account that the system is signed in to,
// or of any server that speaks CalDAV.
Kirigami.Dialog {
    id: dialog

    property string errorText
    // The Google accounts of the system's Online Accounts.
    property var onlineAccounts: []
    readonly property bool google: googleChoice.checked
    readonly property var chosenAccount: onlineAccounts[accountBox.currentIndex] ?? null
    readonly property bool complete: google
        ? chosenAccount !== null && !chosenAccount.added
        : serverField.text.trim().length > 0 && userField.text.trim().length > 0 && passwordField.text.length > 0

    function add(): void {
        if (!complete || Backend.adding) {
            return
        }
        errorText = ""
        if (google) {
            Backend.addGoogleAccount(chosenAccount.id)
        } else {
            Backend.addCalDavAccount(serverField.text, userField.text, passwordField.text)
        }
    }

    function refresh(): void {
        const chosen = chosenAccount?.id
        onlineAccounts = Backend.onlineGoogleAccounts()
        // Stays on the one chosen, or goes to the first that can be added.
        let index = onlineAccounts.findIndex(account => account.id === chosen)
        if (index < 0) {
            index = Math.max(0, onlineAccounts.findIndex(account => !account.added))
        }
        accountBox.currentIndex = index
    }

    title: i18nc("@title:window", "Add Account")
    preferredWidth: Kirigami.Units.gridUnit * 28
    padding: Kirigami.Units.largeSpacing
    standardButtons: Kirigami.Dialog.Cancel

    customFooterActions: Kirigami.Action {
        text: i18nc("@action:button", "Add Account")
        icon.name: "list-add"
        enabled: dialog.complete && !Backend.adding
        onTriggered: dialog.add()
    }

    onOpened: {
        errorText = ""
        passwordField.text = ""
        refresh()
    }
    // Closing while it is under way gives it up.
    onClosed: Backend.cancelAdding()

    // Back from System Settings, there may be a new account.
    Connections {
        target: Qt.application

        function onStateChanged(): void {
            if (dialog.visible && Qt.application.state === Qt.ApplicationActive && !Backend.adding) {
                dialog.refresh()
            }
        }
    }

    Connections {
        target: Backend

        function onAccountAdded(name: string): void {
            dialog.close()
            applicationWindow().showPassiveNotification(i18nc("@info %1 is the name of an account", "“%1” added", name))
        }

        function onAccountFailed(message: string): void {
            dialog.errorText = message
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.errorText.length > 0
            type: Kirigami.MessageType.Error
            text: dialog.errorText
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            enabled: !Backend.adding

            QQC2.RadioButton {
                id: googleChoice

                Kirigami.FormData.label: i18nc("@label where the calendars are", "Calendars from:")
                text: i18nc("@option:radio", "Google")
                checked: true
            }
            QQC2.RadioButton {
                text: i18nc("@option:radio", "Another server")
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 18
                text: dialog.google
                    ? i18nc("@info", "Uses the Google accounts that this system is signed in to. Kalendar does not see the password.")
                    : i18nc("@info", "For Nextcloud, iCloud, Fastmail, and other services that offer CalDAV.")
                wrapMode: Text.WordWrap
                opacity: 0.75
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
            }

            QQC2.ComboBox {
                id: accountBox

                Kirigami.FormData.label: i18nc("@label:listbox", "Account:")
                Layout.fillWidth: true
                visible: dialog.google && dialog.onlineAccounts.length > 0
                model: dialog.onlineAccounts
                textRole: "name"
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 18
                visible: dialog.google && (dialog.onlineAccounts.length === 0 || (dialog.chosenAccount?.added ?? false))
                text: dialog.onlineAccounts.length === 0
                    ? i18nc("@info", "This system is not signed in to a Google account yet.")
                    : i18nc("@info", "The calendars of this account have been added before.")
                wrapMode: Text.WordWrap
            }
            QQC2.Button {
                visible: dialog.google
                text: i18nc("@action:button", "Open Online Accounts…")
                icon.name: "applications-internet"
                onClicked: Backend.openOnlineAccounts()
            }

            QQC2.TextField {
                id: serverField

                Kirigami.FormData.label: i18nc("@label:textbox", "Server address:")
                Layout.fillWidth: true
                visible: !dialog.google
                placeholderText: i18nc("@info:placeholder an example of a server address", "cloud.example.org")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                onAccepted: dialog.add()
            }
            QQC2.TextField {
                id: userField

                Kirigami.FormData.label: i18nc("@label:textbox", "User name:")
                Layout.fillWidth: true
                visible: !dialog.google
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onAccepted: dialog.add()
            }
            Kirigami.PasswordField {
                id: passwordField

                Kirigami.FormData.label: i18nc("@label:textbox", "Password:")
                Layout.fillWidth: true
                visible: !dialog.google
                onAccepted: dialog.add()
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 18
                visible: !dialog.google
                text: i18nc("@info", "Where the service has two-step sign-in, it wants an app password here.")
                wrapMode: Text.WordWrap
                opacity: 0.75
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            visible: Backend.adding
            spacing: Kirigami.Units.smallSpacing

            QQC2.BusyIndicator {
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                running: Backend.adding
            }
            QQC2.Label {
                text: i18nc("@info:progress", "Looking for calendars…")
            }
        }
    }
}
