// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Shows what calendar files hold and asks which calendar their events are
// to go to.
Kirigami.Dialog {
    id: dialog

    property list<url> files
    // What the files hold; see Backend.inspectFiles().
    property var contents: ({ count: 0, events: [], others: 0, error: "" })
    property string calendarId
    property string errorText

    // The events are in the calendar; @p date is when the first one starts.
    signal imported(int count, date date, string calendarName)

    // Files without events are not worth a question: a line says so instead.
    function openFor(urls: list<url>): void {
        const found = Backend.inspectFiles(urls)
        if (found.count === 0) {
            applicationWindow().showPassiveNotification(found.error, "long")
            return
        }
        if (Backend.writableCalendars.length === 0) {
            applicationWindow().showPassiveNotification(i18nc("@info", "None of the calendars can be changed."), "long")
            return
        }
        files = urls
        contents = found
        calendarId = Backend.defaultCalendarId
        errorText = ""
        open()
    }

    function run(): void {
        const name = Backend.writableCalendars.find(calendar => calendar.id === calendarId)?.name ?? ""
        const result = Backend.importFiles(files, calendarId)
        if (result.error.length > 0) {
            errorText = result.error
            return
        }
        close()
        imported(result.count, result.date, name)
    }

    title: i18nc("@title:window", "Import Events")
    preferredWidth: Kirigami.Units.gridUnit * 28
    padding: Kirigami.Units.largeSpacing
    standardButtons: Kirigami.Dialog.Cancel

    customFooterActions: Kirigami.Action {
        text: i18nc("@action:button", "Import")
        icon.name: "document-import"
        onTriggered: dialog.run()
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.errorText.length > 0
            type: Kirigami.MessageType.Error
            text: dialog.errorText
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: dialog.files.length === 1
                ? i18ncp("@info %2 is the name of a file", "“%2” holds one event:", "“%2” holds %1 events:",
                    dialog.contents.count, decodeURIComponent(dialog.files[0].toString().split("/").pop()))
                : i18ncp("@info %2 is a number of files", "The %2 files hold one event:", "The %2 files hold %1 events:",
                    dialog.contents.count, dialog.files.length)
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }

        // The first few, to tell what kind of file it is.
        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Repeater {
                model: dialog.contents.events

                QQC2.Label {
                    required property var modelData

                    Layout.row: index
                    Layout.column: 0
                    required property int index
                    text: modelData.allDay
                        ? modelData.start.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
                        : i18nc("@info %1 is a date, %2 a time of day", "%1, %2",
                            modelData.start.toLocaleDateString(Qt.locale(), Locale.ShortFormat),
                            Backend.formatTime(modelData.start, Backend.use24Hour))
                    opacity: 0.75
                }
            }
            Repeater {
                model: dialog.contents.events

                QQC2.Label {
                    required property var modelData
                    required property int index

                    Layout.row: index
                    Layout.column: 1
                    Layout.fillWidth: true
                    text: modelData.repeats
                        ? i18nc("@info %1 is the title of an event that repeats", "%1 (repeats)", modelData.title)
                        : modelData.title
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            visible: dialog.contents.count > dialog.contents.events.length
            text: i18ncp("@info after a list of events", "and one more", "and %1 more", dialog.contents.count - dialog.contents.events.length)
            opacity: 0.75
        }
        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.contents.others > 0
            text: i18ncp("@info", "One other entry, such as a to-do, is left out: Kalendar keeps events only.",
                "%1 other entries, such as to-dos, are left out: Kalendar keeps events only.", dialog.contents.others)
            wrapMode: Text.WordWrap
            opacity: 0.75
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: calendarBox.count > 1

            CalendarBox {
                id: calendarBox

                Kirigami.FormData.label: i18nc("@label:listbox which calendar imported events go to", "Add to:")
                Kirigami.FormData.buddyFor: calendarBox.box
                Layout.fillWidth: true
                calendarId: dialog.calendarId
                onChosen: calendarId => dialog.calendarId = calendarId
            }
        }
    }
}
