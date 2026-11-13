// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Makes a new event, or shows and changes an existing one.
Kirigami.Dialog {
    id: dialog

    // The event being changed, as the views hand it over; null for a new one.
    property var event: null
    readonly property bool readOnly: event !== null && event.readOnly

    property string calendarId
    property bool allDay: false
    // For events that take whole days, "end" is the last day.
    property date start: new Date()
    property date end: new Date()
    property string repeat: "none"
    property string errorText
    property alias summary: titleField.text
    property alias location: locationField.text
    property alias notes: notesField.text

    readonly property bool endsBeforeStart: allDay
        ? new Date(end.getFullYear(), end.getMonth(), end.getDate()) < new Date(start.getFullYear(), start.getMonth(), start.getDate())
        : end < start

    // Asked for an existing event; the dialog is closed by then.
    signal deleteRequested(var event)

    function openNew(start: date, allDay: bool): void {
        event = null
        calendarId = Backend.defaultCalendarId
        titleField.text = ""
        locationField.text = ""
        notesField.text = ""
        dialog.allDay = allDay
        dialog.start = start
        dialog.end = allDay ? start : new Date(start.getTime() + 60 * 60 * 1000)
        repeat = "none"
        errorText = ""
        open()
    }

    function openFor(existing: var): void {
        event = existing
        calendarId = existing.collectionId
        titleField.text = existing.summary
        locationField.text = existing.location
        notesField.text = existing.description
        allDay = existing.allDay
        start = existing.start
        end = existing.end
        repeat = existing.repeat
        errorText = ""
        open()
    }

    // Moving the start moves the end along, so that the event stays as long as it was.
    function setStart(value: date): void {
        const length = end.getTime() - start.getTime()
        start = value
        end = new Date(value.getTime() + length)
    }

    function setAllDay(value: bool): void {
        if (value === allDay) {
            return
        }
        allDay = value
        if (!value && end.getTime() - start.getTime() < 60 * 1000) {
            // From whole days to times: an hour in the morning of the first day.
            start = new Date(start.getFullYear(), start.getMonth(), start.getDate(), 9)
            end = new Date(end.getFullYear(), end.getMonth(), end.getDate(), 10)
        }
    }

    function save(): void {
        if (readOnly || endsBeforeStart) {
            return
        }
        errorText = Backend.saveEvent({
            collectionId: calendarId,
            originalCollectionId: event ? event.collectionId : "",
            href: event ? event.href : "",
            recurrenceId: event ? event.recurrenceId : undefined,
            slot: event ? event.slot : undefined,
            summary: titleField.text,
            location: locationField.text,
            description: notesField.text,
            allDay: allDay,
            start: start,
            end: end,
            repeat: repeat
        })
        if (errorText.length === 0) {
            close()
        }
    }

    title: event === null ? i18nc("@title:window", "New Event")
        : readOnly ? i18nc("@title:window", "Event") : i18nc("@title:window", "Edit Event")
    preferredWidth: Kirigami.Units.gridUnit * 28
    padding: Kirigami.Units.largeSpacing
    standardButtons: readOnly ? Kirigami.Dialog.Close : Kirigami.Dialog.Cancel

    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Delete")
            icon.name: "edit-delete"
            visible: dialog.event !== null && !dialog.readOnly
            onTriggered: {
                const event = dialog.event
                dialog.close()
                dialog.deleteRequested(event)
            }
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Save")
            icon.name: "document-save"
            visible: !dialog.readOnly
            enabled: !dialog.endsBeforeStart
            onTriggered: dialog.save()
        }
    ]

    onOpened: {
        if (!readOnly) {
            titleField.forceActiveFocus()
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.readOnly
            type: Kirigami.MessageType.Information
            text: i18nc("@info", "This event is in a calendar that cannot be changed.")
        }
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !dialog.readOnly && (dialog.endsBeforeStart || dialog.errorText.length > 0)
            type: Kirigami.MessageType.Error
            text: dialog.endsBeforeStart ? i18nc("@info", "The event ends before it starts. Choose a later end.") : dialog.errorText
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            enabled: !dialog.readOnly

            QQC2.TextField {
                id: titleField

                Kirigami.FormData.label: i18nc("@label:textbox", "Title:")
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder", "What is happening")
                onAccepted: dialog.save()
            }

            RowLayout {
                Kirigami.FormData.label: i18nc("@label when an event starts", "Starts:")
                Kirigami.FormData.buddyFor: startDate
                spacing: Kirigami.Units.smallSpacing

                DateButton {
                    id: startDate
                    date: dialog.start
                    onDateChosen: date => dialog.setStart(Backend.combine(date, dialog.start))
                }
                TimeField {
                    visible: !dialog.allDay
                    value: dialog.start
                    Accessible.name: i18nc("@label:listbox", "Start time")
                    onTimeChosen: time => dialog.setStart(time)
                }
            }
            RowLayout {
                Kirigami.FormData.label: i18nc("@label when an event ends", "Ends:")
                Kirigami.FormData.buddyFor: endDate
                spacing: Kirigami.Units.smallSpacing

                DateButton {
                    id: endDate
                    date: dialog.end
                    onDateChosen: date => dialog.end = Backend.combine(date, dialog.end)
                }
                TimeField {
                    visible: !dialog.allDay
                    value: dialog.end
                    Accessible.name: i18nc("@label:listbox", "End time")
                    onTimeChosen: time => dialog.end = time
                }
            }
            QQC2.CheckBox {
                text: i18nc("@option:check the event has no time of day", "Takes the whole day")
                checked: dialog.allDay
                onToggled: dialog.setAllDay(checked)
            }

            QQC2.ComboBox {
                id: repeatBox

                Kirigami.FormData.label: i18nc("@label:listbox how often an event repeats", "Repeats:")
                // An exception to a repetition does not repeat by itself.
                visible: dialog.repeat !== "exception"
                textRole: "text"
                valueRole: "value"
                model: {
                    const choices = [
                        { value: "none", text: i18nc("@item:inlistbox the event does not repeat", "Never") },
                        { value: "daily", text: i18nc("@item:inlistbox", "Every day") },
                        { value: "weekly", text: i18nc("@item:inlistbox", "Every week") },
                        { value: "monthly", text: i18nc("@item:inlistbox", "Every month") },
                        { value: "yearly", text: i18nc("@item:inlistbox", "Every year") }
                    ]
                    // A rhythm made elsewhere that is none of the above can be kept, though not made here.
                    if (dialog.event !== null && dialog.event.repeat === "custom") {
                        choices.push({ value: "custom", text: i18nc("@item:inlistbox keep how the event repeats now", "In its own rhythm") })
                    }
                    return choices
                }
                currentIndex: Math.max(0, indexOfValue(dialog.repeat))
                onActivated: dialog.repeat = currentValue
            }
            QQC2.Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 18
                visible: dialog.event !== null && dialog.event.repeats && !dialog.readOnly
                text: dialog.repeat === "exception"
                    ? i18nc("@info", "Changes apply to this one day of a repeating event.")
                    : i18nc("@info", "Changes apply to every day the event repeats on.")
                wrapMode: Text.WordWrap
                opacity: 0.75
            }

            CalendarBox {
                id: calendarBox

                Kirigami.FormData.label: i18nc("@label:listbox which calendar an event is kept in", "Calendar:")
                Kirigami.FormData.buddyFor: calendarBox.box
                Layout.fillWidth: true
                visible: !dialog.readOnly && calendarBox.count > 1
                calendarId: dialog.calendarId
                onChosen: calendarId => dialog.calendarId = calendarId
            }

            QQC2.TextField {
                id: locationField

                Kirigami.FormData.label: i18nc("@label:textbox where an event takes place", "Location:")
                Layout.fillWidth: true
                onAccepted: dialog.save()
            }
            QQC2.ScrollView {
                Kirigami.FormData.label: i18nc("@label:textbox", "Notes:")
                Kirigami.FormData.labelAlignment: Qt.AlignTop
                Kirigami.FormData.buddyFor: notesField
                Layout.fillWidth: true
                implicitHeight: Kirigami.Units.gridUnit * 5

                QQC2.TextArea {
                    id: notesField
                    wrapMode: TextEdit.Wrap
                }
            }
        }
    }
}
