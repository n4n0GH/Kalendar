// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Adds the calendars of a Google account, or of any server that speaks CalDAV.
Kirigami.Dialog {
    id: dialog

    property string errorText
    readonly property bool google: googleChoice.checked
    readonly property bool complete: google
        ? Backend.googleConfigured || (clientIdField.text.trim().length > 0 && clientSecretField.text.trim().length > 0)
        : serverField.text.trim().length > 0 && userField.text.trim().length > 0 && passwordField.text.length > 0

    function add(): void {
        if (!complete || Backend.adding) {
            return
        }
        errorText = ""
        if (google) {
            Backend.addGoogleAccount(clientIdField.text, clientSecretField.text)
        } else {
            Backend.addCalDavAccount(serverField.text, userField.text, passwordField.text)
        }
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
    }
    // Closing while it is under way gives it up.
    onClosed: Backend.cancelAdding()

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
                    ? i18nc("@info", "Signing in happens on Google’s page in the web browser. Kalendar does not see the password.")
                    : i18nc("@info", "For Nextcloud, iCloud, Fastmail, and other services that offer CalDAV.")
                wrapMode: Text.WordWrap
                opacity: 0.75
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                visible: !dialog.google || !Backend.googleConfigured
            }

            // Google only lets registered apps sign in. A build of Kalendar
            // that has no registration of its own asks for one.
            QQC2.TextField {
                id: clientIdField

                Kirigami.FormData.label: i18nc("@label:textbox", "Client ID:")
                Layout.fillWidth: true
                visible: dialog.google && !Backend.googleConfigured
                onAccepted: dialog.add()
            }
            Kirigami.PasswordField {
                id: clientSecretField

                Kirigami.FormData.label: i18nc("@label:textbox", "Client secret:")
                Layout.fillWidth: true
                visible: dialog.google && !Backend.googleConfigured
                onAccepted: dialog.add()
            }
            RowLayout {
                visible: dialog.google && !Backend.googleConfigured
                spacing: Kirigami.Units.smallSpacing

                QQC2.Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 18
                    text: i18nc("@info", "This version of Kalendar is not registered with Google, so it needs a registration to sign in with.")
                    wrapMode: Text.WordWrap
                    opacity: 0.75
                }
                Kirigami.ContextualHelpButton {
                    toolTipText: i18nc("@info", "In the Google Cloud console, create a project, switch on the “CalDAV API”, and create an OAuth client of the type “Desktop app”. Its client ID and client secret go here.")
                }
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
                text: dialog.google ? i18nc("@info:progress", "Waiting for the sign-in in the web browser…")
                    : i18nc("@info:progress", "Looking for calendars…")
            }
        }
    }
}
