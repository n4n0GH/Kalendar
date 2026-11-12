// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// An event to read: what a click on it shows. Changing it is one step
// further, so that nothing is changed by accident.
Kirigami.Dialog {
    id: dialog

    // The event, as the views hand it over.
    property var event: null

    // Asked for; the dialog is closed by then.
    signal editRequested(var event)

    readonly property string when: {
        if (event === null) {
            return ""
        }
        const locale = Qt.locale()
        const sameDay = event.start.toDateString() === event.end.toDateString()
        const day = date => date.toLocaleDateString(locale, Locale.LongFormat)
        const time = date => Backend.formatTime(date, Backend.use24Hour)
        if (event.allDay) {
            return sameDay ? day(event.start) : i18nc("@info a range of days", "%1 – %2", day(event.start), day(event.end))
        }
        if (sameDay) {
            return event.start.getTime() === event.end.getTime()
                ? i18nc("@info a day and a time of day", "%1, %2", day(event.start), time(event.start))
                : i18nc("@info a day and a range of times", "%1, %2 – %3", day(event.start), time(event.start), time(event.end))
        }
        return i18nc("@info a range from one day and time to another", "%1, %2 – %3, %4",
            day(event.start), time(event.start), day(event.end), time(event.end))
    }
    readonly property string repeats: {
        switch (event?.repeat ?? "none") {
        case "daily":
            return i18nc("@info how often an event repeats", "Every day")
        case "weekly":
            return i18nc("@info how often an event repeats", "Every week")
        case "monthly":
            return i18nc("@info how often an event repeats", "Every month")
        case "yearly":
            return i18nc("@info how often an event repeats", "Every year")
        case "custom":
            return i18nc("@info how often an event repeats", "In its own rhythm")
        case "exception":
            return i18nc("@info", "One of an event that repeats, changed for this time")
        }
        return ""
    }

    function openFor(existing: var): void {
        event = existing
        open()
    }

    title: event?.title ?? ""
    preferredWidth: Kirigami.Units.gridUnit * 28
    padding: Kirigami.Units.largeSpacing
    standardButtons: Kirigami.Dialog.Close

    customFooterActions: Kirigami.Action {
        text: i18nc("@action:button", "Edit")
        icon.name: "document-edit"
        visible: dialog.event !== null && !dialog.event.readOnly
        onTriggered: {
            const event = dialog.event
            dialog.close()
            dialog.editRequested(event)
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // Each line: a sign for what it is about, and the words.
        component Line: RowLayout {
            property alias icon: sign.source
            property alias text: words.text
            property alias textFormat: words.textFormat

            Layout.fillWidth: true
            visible: words.text.length > 0
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                id: sign

                Layout.alignment: Qt.AlignTop
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
            }
            Kirigami.SelectableLabel {
                id: words

                Layout.fillWidth: true
                // As wide as there is room, not as wide as a long line
                // would like to be, which would stretch the dialog.
                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }

        Line {
            icon: "clock"
            text: dialog.when
        }
        Line {
            icon: "view-refresh"
            text: dialog.repeats
        }
        Line {
            icon: "mark-location"
            text: dialog.event?.location ?? ""
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Item {
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium

                Rectangle {
                    anchors.centerIn: parent
                    width: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
                    height: width
                    radius: height / 2
                    color: dialog.event?.color ?? "transparent"
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: dialog.event === null ? ""
                    : dialog.event.accountName.length > 0
                        ? i18nc("@info %1 is the name of a calendar, %2 of the account it belongs to", "%1 (%2)", dialog.event.calendarName, dialog.event.accountName)
                        : dialog.event.calendarName
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            visible: notes.visible
        }
        // Notes may be long, and may be written as HTML, which Google does.
        // Long ones scroll in their own place, so that the rest stays put.
        RowLayout {
            id: notes

            readonly property string text: (dialog.event?.description ?? "").trim().length > 0 ? Backend.renderNotes(dialog.event.description.trim()) : ""

            Layout.fillWidth: true
            visible: text.length > 0
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Icon {
                Layout.alignment: Qt.AlignTop
                implicitWidth: Kirigami.Units.iconSizes.smallMedium
                implicitHeight: Kirigami.Units.iconSizes.smallMedium
                source: "view-list-text"
            }
            QQC2.ScrollView {
                id: scroll

                Layout.fillWidth: true
                Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                Layout.preferredHeight: Math.min(noteWords.implicitHeight, Kirigami.Units.gridUnit * 14)
                contentWidth: availableWidth
                // Room for the bar to scroll with at all times, so that the
                // words are not laid out anew when it comes and goes.
                rightPadding: Kirigami.Units.gridUnit
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                Kirigami.SelectableLabel {
                    id: noteWords

                    width: scroll.availableWidth
                    textFormat: Text.RichText
                    wrapMode: Text.Wrap
                    text: notes.text
                    onLinkActivated: link => Qt.openUrlExternally(link)
                }
            }
        }
    }
}
