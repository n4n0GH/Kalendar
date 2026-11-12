<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# Interface

The interface is QML with Kirigami, in `src/qml/`, built into the executable
as the module `io.github.n4n0gh.kalendar`. It follows the
[Kirigami Design Guide](https://n4n0gh.github.io/Doku): colours come from
`Kirigami.Theme`, sizes and durations from `Kirigami.Units`, icons from the
icon theme by name, and nothing is positioned by numbers of pixels.

## Layout

```
┌ November 2026 — Kalendar ─────────────────────────────────────────┐
│ Sidebar        │ ◧ ‹ Today › New Event     Day Week Month Agenda ⋮ │
│  MiniMonth     ├────────────────────────────────────────────────────┤
│  calendars     │                                                    │
│  with ticks    │   MonthView, or WeekView for week, day and agenda  │
│                │                                                    │
│ (Fetching…)    │                                                    │
└────────────────┴────────────────────────────────────────────────────┘
```

The window (`Main.qml`) holds the two things everything revolves around:
`selectedDate` and `viewMode` (`month`, `week`, `day` or `agenda`). The
sidebar and the page both read and set them. What the calendar shows is in
the title bar of the window; the place a page title usually has in the
toolbar holds the buttons to move about instead (`titleDelegate` in
`CalendarPage.qml`).

Settings and About are pushed as layers on the page stack. Dialogs
(`EventViewDialog`, `EventDialog`, `AddAccountDialog`) are created once in
`Main.qml` and opened through functions of the window: `showEvent()`,
`editEvent()`, `newEvent()`, `newEventAt()`, `addAccount()`.

## Files

| File | What it is |
| --- | --- |
| `Main.qml` | The window: state, dialogs, the functions pages call |
| `Sidebar.qml` | The global drawer: small month, calendars, the bar that shows fetching |
| `MiniMonth.qml` | The small month, with week numbers if wanted |
| `CalendarPage.qml` | The one page: toolbar, and the view for the mode |
| `MonthView.qml` | Six weeks, each laid out by `Backend.weekLayout()` |
| `WeekView.qml` | Days side by side with hours down the side: one day, a week, or the agenda |
| `EventChip.qml` | One event in either view: a dot and a title, a bar, or a block |
| `EventViewDialog.qml` | An event to read |
| `EventDialog.qml` | An event to make or change |
| `DateButton.qml`, `TimeField.qml` | Picking a day; typing or picking a time |
| `CalendarLabel.qml` | A calendar in a list: colour, name, where it is kept |
| `AddAccountDialog.qml` | Adding an account |
| `SettingsPage.qml` | Accounts, how the calendar looks, the clock plugin |

## How views get their events

Views do no arithmetic on events. They ask the engine, through `Backend`,
for a finished layout and only draw it:

- `weekLayout(start, days, barsOnly, maxLanes, revision)` gives the events of
  some days as *segments*, each with a row ("lane") so that none overlap, and
  per day how many did not fit. The month view calls it once per week; the
  week view calls it with `barsOnly` for the strip of whole-day events.
- `dayLayout(day, revision)` gives the events with a time of day as blocks,
  each with a column so that events at the same time sit side by side.

Each entry is also the event itself, as a map (`Engine::toVariant()`): that
map is what is handed to `showEvent()` and `editEvent()`, and what
`saveEvent()` and `deleteEvent()` take back.

The agenda is `WeekView` with `wholeWeek` off: it starts at the selected day
rather than at the first day of its week, for as many days as the setting
says.

## QML conventions that bite

- **Hand `Backend.revision` in as an argument.** `revision` goes up whenever
  events may look different. Reading it inside a binding is not enough: the
  QML compiler drops a read whose value is not used, and the view never
  refreshes. So the layout functions take it as a parameter they ignore, and
  so do `formatTime(time, use24Hour)` and `hourText()` with the setting they
  depend on.
- **Radio buttons need a `ButtonGroup` per question.** All radio buttons with
  the same parent exclude each other, and in a `FormLayout` that is all of
  them on the page.
- **A `ComboBox` of the desktop style cannot have its `contentItem`
  replaced**; the style expects a text field there. The calendar chooser in
  `EventDialog` therefore has its coloured dot beside the box, and a
  `displayText`.
- **Text that wraps must not ask for its full width in a dialog.** A long
  line makes the dialog as wide as the window; and content taller than the
  window that also wraps makes Kirigami's dialog lay itself out in a loop.
  `EventViewDialog` gives its labels a small preferred width and lets long
  notes scroll in a place of their own.
- **Closing an `InlineMessage` by its button cuts its `visible` binding.**
  Bind it again in `onVisibleChanged` if it is to come back.
- **The reopening button of the sidebar is Kirigami's own.** It appears in the
  toolbar by itself when the drawer is closed; `Sidebar.qml` only gives it
  icon and tooltip, and follows `drawerOpen` to remember the state.

## Settings

| Setting | Where it lives |
| --- | --- |
| Window size, view mode, agenda length, sidebar put away | `Settings` in `Main.qml` |
| 12 or 24 hours, first day of the week, week numbers | Properties of `Backend`, in `QSettings` |
| The calendar used last for a new event | `Backend`, in `QSettings` |
| Which calendars are shown | The database (`collections.enabled`) |

The first two of the `Backend` settings follow the system's language and
region until the user chooses.

`SettingsPage` works like System Settings: its controls change properties of
the page only, *Apply* writes them, *Cancel* and the back arrow forget them.
Adding and removing accounts and installing the plugin are things done, not
settings, and happen at once.

Times of day are written by `Backend::formatTime()` everywhere, which takes
the format of the user's language and makes it count to 24 or twice to 12.
`Backend::parseTime()` reads what people type ("9", "930", "2 pm") in either
way, whatever is set.

## Notes

`Backend::renderNotes()` makes the notes of an event fit to show. Notes
written as HTML (which Google does) keep their formatting; in plain notes,
line breaks are kept and addresses become links. Either way only a fixed list
of formatting tags gets through, without attributes, and links only to web,
mail and phone addresses. Pictures, scripts, styles and frames are removed,
so that opening an event fetches nothing and runs nothing: invitations from
strangers end up in calendars.

Editing shows the notes as they are stored.

## For tests and pictures

With `KALENDAR_TEST_MODE` set, `main.cpp` offers three more variables:
`KALENDAR_DEMO` fills an empty calendar with a few events,
`KALENDAR_EVAL` runs JavaScript in the scope of `Main.qml` half a second
after start, and `KALENDAR_SCREENSHOT` saves a picture of the window after
two and a half seconds and quits. Together they drive the interface without
a display; see [Development](development.md#running-and-debugging).
