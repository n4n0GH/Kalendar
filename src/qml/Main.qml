// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.coreaddons as KCoreAddons
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    // The day everything revolves around: the views show the month, week
    // or day it is in, and new events start on it.
    property date selectedDate: new Date()
    // "month", "week" or "day".
    property string viewMode: "month"
    // How many days the agenda shows: 3 or 7.
    property int agendaDays: 3

    // What the calendar shows, such as “November 2026”, then the app's name.
    title: pageStack.depth > 0 && pageStack.get(0)?.title
        ? i18nc("@title:window %1 is what the calendar shows, such as “November 2026”", "%1 — Kalendar", pageStack.get(0).title)
        : i18nc("@title:window", "Kalendar")
    width: Kirigami.Units.gridUnit * 60
    height: Kirigami.Units.gridUnit * 38
    minimumWidth: Kirigami.Units.gridUnit * 18
    minimumHeight: Kirigami.Units.gridUnit * 20

    Settings {
        property alias width: root.width
        property alias height: root.height
        property alias viewMode: root.viewMode
        property alias agendaDays: root.agendaDays
        property alias sidebarCollapsed: sidebar.collapsed
    }

    // A new event on the selected day: in an hour from now if that is today,
    // otherwise in the morning.
    function newEvent(): void {
        const now = new Date()
        const day = root.selectedDate
        const today = day.toDateString() === now.toDateString()
        const hour = today ? Math.min(now.getHours() + 1, 23) : 9
        newEventAt(new Date(day.getFullYear(), day.getMonth(), day.getDate(), hour), false)
    }

    function newEventAt(start: date, allDay: bool): void {
        if (Backend.writableCalendars.length === 0) {
            showPassiveNotification(i18nc("@info", "None of the calendars can be changed."), "long")
            return
        }
        eventDialog.openNew(start, allDay)
    }

    // An event is read first; changing it is a step further.
    function showEvent(event: var): void {
        eventViewDialog.openFor(event)
    }

    function editEvent(event: var): void {
        eventDialog.openFor(event)
    }

    function deleteEvent(event: var, wholeSeries: bool): void {
        const token = Backend.deleteEvent(event, wholeSeries)
        if (token !== 0) {
            // Deleting is done at once and can be taken back for a few seconds.
            showPassiveNotification(i18nc("@info %1 is the title of an event", "“%1” deleted", event.title), "long",
                i18nc("@action:button", "Undo"), () => Backend.undo(token))
        }
    }

    function addAccount(): void {
        addAccountDialog.open()
    }

    function showSettings(): void {
        if (!(pageStack.layers.currentItem instanceof SettingsPage)) {
            pageStack.layers.push(settingsPage)
        }
    }

    function showAbout(): void {
        if (!(pageStack.layers.currentItem instanceof Kirigami.AboutPage)) {
            pageStack.layers.push(aboutPage)
        }
    }

    globalDrawer: Sidebar {
        id: sidebar

        selectedDate: root.selectedDate
        onDateChosen: date => root.selectedDate = date
    }

    pageStack.initialPage: CalendarPage {
        sidebar: sidebar
        selectedDate: root.selectedDate
        viewMode: root.viewMode
        agendaDays: root.agendaDays
        onDateSelected: date => root.selectedDate = date
        onViewModeRequested: mode => root.viewMode = mode
    }

    Component {
        id: settingsPage

        SettingsPage {
        }
    }

    Component {
        id: aboutPage

        Kirigami.AboutPage {
            aboutData: KCoreAddons.AboutData
        }
    }

    EventViewDialog {
        id: eventViewDialog

        onEditRequested: event => root.editEvent(event)
    }

    EventDialog {
        id: eventDialog

        onDeleteRequested: event => {
            if (event.seriesRepeats) {
                // Asked only once the first dialog is gone: never two at a time.
                deleteRepeatingDialog.event = event
                deleteRepeatingDialog.open()
            } else {
                root.deleteEvent(event, true)
            }
        }
    }

    Kirigami.PromptDialog {
        id: deleteRepeatingDialog

        property var event: null

        title: i18nc("@title:window", "Delete Repeating Event")
        subtitle: event ? i18nc("@info %1 is the title of an event, %2 a date",
            "“%1” repeats. Delete only the one on %2, or all of them?",
            event.title, event.start.toLocaleDateString(Qt.locale(), Locale.LongFormat)) : ""
        standardButtons: Kirigami.Dialog.Cancel

        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Delete This One")
                icon.name: "edit-delete-remove"
                onTriggered: {
                    deleteRepeatingDialog.close()
                    root.deleteEvent(deleteRepeatingDialog.event, false)
                }
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Delete All")
                icon.name: "edit-delete"
                onTriggered: {
                    deleteRepeatingDialog.close()
                    root.deleteEvent(deleteRepeatingDialog.event, true)
                }
            }
        ]
    }

    AddAccountDialog {
        id: addAccountDialog
    }

    Connections {
        target: Backend

        function onNotice(text: string): void {
            root.showPassiveNotification(text, "long")
        }
    }
}
