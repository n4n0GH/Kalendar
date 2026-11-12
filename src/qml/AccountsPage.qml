// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigami.delegates as KD

// The accounts calendars come from: to add one, or to remove one.
Kirigami.ScrollablePage {
    id: page

    signal addAccountRequested()

    title: i18nc("@title", "Accounts")

    Kirigami.PromptDialog {
        id: removeDialog

        property var account: null

        title: i18nc("@title:window", "Remove Account")
        subtitle: account ? i18nc("@info %1 is the name of an account",
            "“%1” and its calendars will be removed from this system. The events stay on the server.", account.name) : ""
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

    ListView {
        currentIndex: -1
        model: Backend.accounts

        header: Kirigami.InlineViewHeader {
            width: ListView.view.width
            text: i18nc("@title:group", "Accounts")
            actions: Kirigami.Action {
                text: i18nc("@action:button", "Add Account…")
                icon.name: "list-add"
                onTriggered: page.addAccountRequested()
            }
        }

        delegate: QQC2.ItemDelegate {
            id: row

            required property var modelData
            readonly property bool failed: modelData.error.length > 0

            width: ListView.view.width
            // Nothing happens on a click; the row only shows the account.
            hoverEnabled: false
            down: false
            text: modelData.name
            Accessible.description: failed ? modelData.error : modelData.description

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                KD.IconTitleSubtitle {
                    Layout.fillWidth: true
                    title: row.modelData.name
                    // A problem is told in words and with a warning sign, not by colour.
                    subtitle: row.failed ? row.modelData.error : row.modelData.description
                    icon.name: row.failed ? "data-warning"
                        : row.modelData.kind === "local" ? "computer" : "folder-cloud"
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
    }
}
