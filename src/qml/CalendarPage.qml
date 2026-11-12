// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// The one page of the app: the calendar, as a month, a week or a day.
Kirigami.Page {
    id: page

    property date selectedDate: new Date()
    property string viewMode: "month"

    signal dateSelected(date date)
    signal viewModeRequested(string mode)

    readonly property int firstDayOfWeek: Qt.locale().firstDayOfWeek
    readonly property date weekStart: new Date(selectedDate.getFullYear(), selectedDate.getMonth(),
        selectedDate.getDate() - (selectedDate.getDay() - firstDayOfWeek + 7) % 7)

    // One month, week or day back or ahead.
    function step(direction: int): void {
        const day = selectedDate
        if (viewMode === "month") {
            // The same day of the other month, or its last if it is shorter.
            const length = new Date(day.getFullYear(), day.getMonth() + direction + 1, 0).getDate()
            dateSelected(new Date(day.getFullYear(), day.getMonth() + direction, Math.min(day.getDate(), length)))
        } else {
            dateSelected(new Date(day.getFullYear(), day.getMonth(), day.getDate() + direction * (viewMode === "week" ? 7 : 1)))
        }
    }

    padding: 0

    title: {
        const locale = Qt.locale()
        if (viewMode === "day") {
            return selectedDate.toLocaleDateString(locale, Locale.LongFormat)
        }
        if (viewMode === "week") {
            const last = new Date(weekStart.getFullYear(), weekStart.getMonth(), weekStart.getDate() + 6)
            if (last.getMonth() !== weekStart.getMonth()) {
                return i18nc("@title a week that spans two months, such as “Nov – Dec 2026”", "%1 – %2",
                    weekStart.toLocaleDateString(locale, weekStart.getFullYear() === last.getFullYear() ? "MMM" : "MMM yyyy"),
                    last.toLocaleDateString(locale, "MMM yyyy"))
            }
        }
        return selectedDate.toLocaleDateString(locale, "MMMM yyyy")
    }

    actions: [
        Kirigami.Action {
            text: i18nc("@action:button", "New Event")
            icon.name: "appointment-new"
            shortcut: StandardKey.New
            displayHint: Kirigami.DisplayHint.KeepVisible
            onTriggered: applicationWindow().newEvent()
        },
        Kirigami.Action {
            text: i18nc("@action:button go to the current day", "Today")
            icon.name: "go-jump-today"
            shortcut: "Ctrl+T"
            onTriggered: page.dateSelected(new Date())
        },
        Kirigami.Action {
            text: page.viewMode === "month" ? i18nc("@action:button", "Previous Month")
                : page.viewMode === "week" ? i18nc("@action:button", "Previous Week") : i18nc("@action:button", "Previous Day")
            icon.name: "go-previous"
            shortcut: StandardKey.Back
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: page.step(-1)
        },
        Kirigami.Action {
            text: page.viewMode === "month" ? i18nc("@action:button", "Next Month")
                : page.viewMode === "week" ? i18nc("@action:button", "Next Week") : i18nc("@action:button", "Next Day")
            icon.name: "go-next"
            shortcut: StandardKey.Forward
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: page.step(1)
        },
        Kirigami.Action {
            text: i18nc("@action:button show one day", "Day")
            icon.name: "view-calendar-day"
            shortcut: "Ctrl+1"
            checkable: true
            checked: page.viewMode === "day"
            QQC2.ActionGroup.group: viewGroup
            onTriggered: page.viewModeRequested("day")
        },
        Kirigami.Action {
            text: i18nc("@action:button show one week", "Week")
            icon.name: "view-calendar-week"
            shortcut: "Ctrl+2"
            checkable: true
            checked: page.viewMode === "week"
            QQC2.ActionGroup.group: viewGroup
            onTriggered: page.viewModeRequested("week")
        },
        Kirigami.Action {
            text: i18nc("@action:button show one month", "Month")
            icon.name: "view-calendar-month"
            shortcut: "Ctrl+3"
            checkable: true
            checked: page.viewMode === "month"
            QQC2.ActionGroup.group: viewGroup
            onTriggered: page.viewModeRequested("month")
        },
        Kirigami.Action {
            text: i18nc("@action:button fetch changes from the servers", "Refresh")
            icon.name: "view-refresh"
            shortcut: StandardKey.Refresh
            enabled: !Backend.syncing
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: Backend.sync()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Accounts")
            icon.name: "user-identity"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: applicationWindow().showAccounts()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "About Kalendar")
            icon.name: "help-about"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: applicationWindow().showAbout()
        }
    ]

    QQC2.ActionGroup {
        id: viewGroup
    }

    header: Kirigami.InlineMessage {
        position: Kirigami.InlineMessage.Position.Header
        type: Kirigami.MessageType.Warning
        visible: Backend.errorText.length > 0
        text: Backend.errorText

        actions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Try Again")
                icon.name: "view-refresh"
                enabled: !Backend.syncing
                onTriggered: Backend.sync()
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Show Accounts")
                icon.name: "user-identity"
                onTriggered: applicationWindow().showAccounts()
            }
        ]
    }

    Loader {
        anchors.fill: parent
        sourceComponent: page.viewMode === "month" ? monthView : weekView
    }

    Component {
        id: monthView

        MonthView {
            selectedDate: page.selectedDate
            onDateSelected: date => page.dateSelected(date)
            onStepRequested: direction => page.step(direction)
            onNewEventRequested: date => {
                page.dateSelected(date)
                applicationWindow().newEvent()
            }
            onEventActivated: event => applicationWindow().editEvent(event)
            onDayActivated: date => {
                page.dateSelected(date)
                page.viewModeRequested("day")
            }
        }
    }

    Component {
        id: weekView

        WeekView {
            selectedDate: page.selectedDate
            dayCount: page.viewMode === "day" ? 1 : 7
            onDateSelected: date => page.dateSelected(date)
            onNewEventRequested: (start, allDay) => {
                page.dateSelected(start)
                applicationWindow().newEventAt(start, allDay)
            }
            onEventActivated: event => applicationWindow().editEvent(event)
            onDayActivated: date => {
                page.dateSelected(date)
                page.viewModeRequested("day")
            }
        }
    }
}
