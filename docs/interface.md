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
│ Sidebar        │ ◧ ‹ Today › New Event         Agenda Week Month ⋮ │
│  MiniMonth     ├────────────────────────────────────────────────────┤
│  calendars     │                                                    │
│  with ticks    │   AgendaView, WeekView or MonthView                │
│                │                                                    │
│ (Fetching…)    │                                                    │
└────────────────┴────────────────────────────────────────────────────┘
```

The window (`Main.qml`) holds the two things everything revolves around:
`selectedDate` and `viewMode` (`agenda`, `week` or `month`). The
sidebar and the page both read and set them. What the calendar shows is in
the title bar of the window; the place a page title usually has in the
toolbar holds the buttons to move about instead (`titleDelegate` in
`CalendarPage.qml`).

Settings and About are pushed as layers on the page stack. Dialogs
(`EventViewDialog`, `EventDialog`, `AddAccountDialog`, `ImportDialog`) are
created once in `Main.qml` and opened through functions of the window:
`showEvent()`, `editEvent()`, `newEvent()`, `newEventAt()`, `addAccount()`,
`importFiles()`.

## Files

| File | What it is |
| --- | --- |
| `Main.qml` | The window: state, dialogs, the functions pages call |
| `Sidebar.qml` | The global drawer: small month, calendars, the bar that shows fetching |
| `MiniMonth.qml` | The small month, with week numbers if wanted |
| `CalendarPage.qml` | The one page: toolbar, and the view for the mode |
| `MonthView.qml` | Six weeks, each laid out by `Backend.weekLayout()` |
| `AgendaView.qml` | The selected day and those after it, one below the other, each with its events stacked |
| `WeekView.qml` | The days of a week side by side, with hours down the side |
| `EventChip.qml` | One event in any view: a dot and a title, a bar, a block, or a row |
| `EventViewDialog.qml` | An event to read |
| `EventDialog.qml` | An event to make or change |
| `DateButton.qml`, `TimeField.qml` | Picking a day; typing or picking a time |
| `CalendarLabel.qml` | A calendar in a list: colour, name, where it is kept |
| `CalendarBox.qml` | Choosing the calendar events go to |
| `ImportDialog.qml` | What calendar files hold, and where their events are to go |
| `AddAccountDialog.qml` | Adding an account |
| `SettingsPage.qml` | Accounts, how the calendar looks, the clock plugin |

## Importing calendar files

Calendar files (`.ics`, iCalendar) reach the window in four ways, which all
end in `importFiles()`: *Import…* in the menu (a `FileDialog`), dropping
files on the window, naming them on the command line, and opening them from
the file manager, for which the desktop file names `text/calendar`. That is
also what makes Kalendar a choice for the system's calendar application,
which the *Add* button of Plasma's clock starts.

`ImportDialog` shows what the files hold (`Backend.inspectFiles()`) and asks
which calendar the events are to go to; files without events only get a line
at the bottom of the window. `Backend.importFiles()` hands each file to
`Engine::importEvents()`:

- Every event becomes an item of its own, together with the exceptions to
  its repetition (they share its UID), and is sent to the calendar's server
  like an event made here.
- An event the calendar has already, by its UID, is replaced. Importing a
  file twice changes nothing; importing a newer version of it updates.
- To-dos and journal entries are left out, and the dialog says how many.

If Kalendar is running already, the files go to its window: see
[Architecture](architecture.md#one-kalendar-at-a-time).

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

The agenda (`AgendaView`), which Kalendar opens with, has no hours down the
side: an evening with one event would be a long way to scroll to, and
nothing would say that it is there. It is a list of days, each with all its
events as rows of the same height (`weekLayout()` for that one day, which
puts whole days first and the others by their time); a day without events is
listed with nothing beside it. The list starts at the selected day and has as
many days as the setting says, so it scrolls only when their events do not
fit. The arrows in the toolbar go that many days back or ahead. Making an
event on a day in the list leaves the selected day alone, so that the list
stays where it is (`newEventOn()`).

## Making an event

*New Event* in the toolbar makes one on the selected day. A double click
makes one where it was: on a day in the month and in the agenda, at a
time in the week. A right click in any view brings up a menu with *New
Event…* for the same place (`menuRequested()` of the views,
`showContextMenu()` in `CalendarPage.qml`).

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
