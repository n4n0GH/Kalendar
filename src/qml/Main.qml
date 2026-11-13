// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import org.kde.coreaddons as KCoreAddons
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    // The day everything revolves around: the views show the month, week
    // or day it is in, and new events start on it.
    property date selectedDate: new Date()
    // "agenda", "week" or "month".
    property string viewMode: "agenda"
    // Earlier versions had a view of one day, and may have been closed in it.
    onViewModeChanged: {
        if (viewMode === "day") {
            viewMode = "agenda"
        }
    }
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
        property alias foldedAccounts: sidebar.foldedAccounts
    }

    // A new event on the selected day: in an hour from now if that is today,
    // otherwise in the morning.
    function newEvent(): void {
        newEventOn(root.selectedDate)
    }

    // The same on another day.
    function newEventOn(day: date): void {
        const now = new Date()
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

    // What a right click on a day brings up; @p create makes the event there.
    function showContextMenu(create: var): void {
        contextMenu.create = create
        contextMenu.popup()
    }

    // The menu is drawn inside the window, which hears nothing of clicks on
    // its title bar or on other windows. So it goes when the pointer leaves
    // the window (main.cpp tells) and when another window is worked in.
    function dismissMenu(): void {
        contextMenu.dismiss()
    }

    onActiveChanged: {
        if (!active) {
            dismissMenu()
        }
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

    // Asks for calendar files to import.
    function chooseFilesToImport(): void {
        importFileDialog.open()
    }

    // Shows what the files hold and asks where their events are to go.
    function importFiles(files: list<url>): void {
        // Never two dialogs at a time: whatever was open gives way.
        for (const open of [eventViewDialog, eventDialog, deleteRepeatingDialog, addAccountDialog, importDialog]) {
            open.close()
        }
        importDialog.openFor(files)
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
        onMenuRequested: date => root.showContextMenu(() => root.newEventOn(date))
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

    QQC2.Menu {
        id: contextMenu

        property var create: null

        QQC2.MenuItem {
            action: Kirigami.Action {
                text: i18nc("@action:inmenu", "New Event…")
                icon.name: "appointment-new"
                onTriggered: contextMenu.create()
            }
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

    FileDialog {
        id: importFileDialog

        title: i18nc("@title:window", "Import Events")
        fileMode: FileDialog.OpenFiles
        nameFilters: [i18nc("@item:inlistbox a kind of file", "Calendar files (*.ics *.ical *.ifb *.icalendar)"),
            i18nc("@item:inlistbox a kind of file", "All files (*)")]
        onAccepted: root.importFiles(selectedFiles)
    }

    ImportDialog {
        id: importDialog

        onImported: (count, date, calendarName) => {
            // To where the events are, which may be far from today.
            root.selectedDate = date
            root.showPassiveNotification(i18ncp("@info %2 is the name of a calendar",
                "One event added to “%2”", "%1 events added to “%2”", count, calendarName), "long")
        }
    }

    // Calendar files dropped on the window are imported like opened ones.
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: drop => {
            if (drop.hasUrls) {
                drop.accept(Qt.CopyAction)
                root.importFiles(drop.urls)
            }
        }
    }

    Connections {
        target: Backend

        function onNotice(text: string): void {
            root.showPassiveNotification(text, "long")
        }

        // From the file manager or the command line.
        function onFilesOpened(files: list<url>): void {
            root.importFiles(files)
        }
    }
}
