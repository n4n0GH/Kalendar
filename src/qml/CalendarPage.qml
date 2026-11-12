// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// The one page of the app: the calendar, as a month, a week or a day.
Kirigami.Page {
    id: page

    property date selectedDate: new Date()
    // "month", "week", "day", or "agenda": the selected day and those after it.
    property string viewMode: "month"
    // How many days the agenda shows.
    property int agendaDays: 3
    required property Sidebar sidebar

    signal dateSelected(date date)
    signal viewModeRequested(string mode)

    readonly property int firstDayOfWeek: Backend.firstDayOfWeek
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
            const days = viewMode === "week" ? 7 : viewMode === "agenda" ? agendaDays : 1
            dateSelected(new Date(day.getFullYear(), day.getMonth(), day.getDate() + direction * days))
        }
    }

    padding: 0

    title: {
        const locale = Qt.locale()
        if (viewMode === "day") {
            return selectedDate.toLocaleDateString(locale, Locale.LongFormat)
        }
        if (viewMode === "week" || viewMode === "agenda") {
            const first = viewMode === "week" ? weekStart : selectedDate
            const last = new Date(first.getFullYear(), first.getMonth(), first.getDate() + (viewMode === "week" ? 7 : agendaDays) - 1)
            if (last.getMonth() !== first.getMonth()) {
                return i18nc("@title days that span two months, such as “Nov – Dec 2026”", "%1 – %2",
                    first.toLocaleDateString(locale, first.getFullYear() === last.getFullYear() ? "MMM" : "MMM yyyy"),
                    last.toLocaleDateString(locale, "MMM yyyy"))
            }
        }
        return selectedDate.toLocaleDateString(locale, "MMMM yyyy")
    }

    // In place of the title, which the title bar of the window shows.
    titleDelegate: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.ToolButton {
            // While the sidebar is put away, the toolbar has a button of
            // its own in this place that brings it back.
            visible: !page.sidebar.modal && !page.sidebar.collapsed
            text: i18nc("@action:button", "Hide Sidebar")
            icon.name: "sidebar-collapse-left"
            display: QQC2.AbstractButton.IconOnly
            onClicked: page.sidebar.collapsed = true

            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            Shortcut {
                sequence: "F9"
                enabled: !page.sidebar.modal
                onActivated: page.sidebar.collapsed = !page.sidebar.collapsed
            }
        }
        Repeater {
            model: [page.previousAction, page.todayAction, page.nextAction, page.newEventAction]

            QQC2.ToolButton {
                required property Kirigami.Action modelData
                readonly property bool iconOnly: modelData === page.previousAction || modelData === page.nextAction

                action: modelData
                display: iconOnly ? QQC2.AbstractButton.IconOnly : QQC2.AbstractButton.TextBesideIcon

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: iconOnly && hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
        }
    }

    // These are at the start of the toolbar, where a page's title usually
    // is; the title is in the title bar of the window instead.
    readonly property Kirigami.Action newEventAction: Kirigami.Action {
        text: i18nc("@action:button", "New Event")
        icon.name: "appointment-new"
        shortcut: StandardKey.New
        onTriggered: applicationWindow().newEvent()
    }
    readonly property Kirigami.Action todayAction: Kirigami.Action {
        text: i18nc("@action:button go to the current day", "Today")
        icon.name: "go-jump-today"
        shortcut: "Ctrl+T"
        onTriggered: page.dateSelected(new Date())
    }
    readonly property Kirigami.Action previousAction: Kirigami.Action {
        text: page.viewMode === "month" ? i18nc("@action:button", "Previous Month")
            : page.viewMode === "week" ? i18nc("@action:button", "Previous Week")
            : page.viewMode === "agenda" ? i18nc("@action:button", "Previous Days") : i18nc("@action:button", "Previous Day")
        icon.name: "go-previous"
        shortcut: StandardKey.Back
        onTriggered: page.step(-1)
    }
    readonly property Kirigami.Action nextAction: Kirigami.Action {
        text: page.viewMode === "month" ? i18nc("@action:button", "Next Month")
            : page.viewMode === "week" ? i18nc("@action:button", "Next Week")
            : page.viewMode === "agenda" ? i18nc("@action:button", "Next Days") : i18nc("@action:button", "Next Day")
        icon.name: "go-next"
        shortcut: StandardKey.Forward
        onTriggered: page.step(1)
    }

    actions: [
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
            text: i18nc("@action:button show the days that come next", "Agenda")
            icon.name: "view-calendar-agenda"
            shortcut: "Ctrl+4"
            checkable: true
            checked: page.viewMode === "agenda"
            QQC2.ActionGroup.group: viewGroup
            onTriggered: page.viewModeRequested("agenda")
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
            text: i18nc("@action:button", "Settings")
            icon.name: "configure"
            shortcut: StandardKey.Preferences
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: applicationWindow().showSettings()
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
                onTriggered: applicationWindow().showSettings()
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
            onEventActivated: event => applicationWindow().showEvent(event)
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
            dayCount: page.viewMode === "day" ? 1 : page.viewMode === "agenda" ? page.agendaDays : 7
            wholeWeek: page.viewMode !== "agenda"
            onDateSelected: date => page.dateSelected(date)
            onNewEventRequested: (start, allDay) => {
                // The agenda starts at the selected day, and is to stay where it is.
                if (page.viewMode !== "agenda") {
                    page.dateSelected(start)
                }
                applicationWindow().newEventAt(start, allDay)
            }
            onEventActivated: event => applicationWindow().showEvent(event)
            onDayActivated: date => {
                page.dateSelected(date)
                page.viewModeRequested("day")
            }
        }
    }
}
