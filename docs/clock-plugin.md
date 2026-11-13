<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# The clock plugin

Plasma's clock opens a calendar, and that calendar shows events from plugins
(`CalendarEvents::CalendarEventsPlugin`, from KDeclarative).
`src/plasma/` is such a plugin for Kalendar's events; `src/clockplugin.*`
installs it.

## What it does

The plugin runs inside the Plasma shell, so it only ever reads. When Plasma
asks for the events of a range of days (`loadEventsForDateRange()`), it opens
Kalendar's database read-only through the same `Engine` the app uses
(`openReadOnly()`), and hands over every appearance of an event in the
calendars that are shown in Kalendar. An event that takes several days is
listed on each of them.

Opening the database means reading every event there is, which takes a
moment (a tenth of a second for two thousand events), and Plasma asks often:
twice whenever the calendar is opened, and for every month that is shown.
So the shell is never kept waiting for it:

- A thread of its own does the reading (`Reader`). `loadEventsForDateRange()`
  returns at once and the events follow with `dataReady()`.
- The reader keeps the database as it read it, and reads it again only when
  the files have another size or time of change than they had then.
- Plasma drops what it has every time it asks, so only the last question is
  answered: an answer to an earlier one would come on top of it.

The plugin also watches the database file and asks the reader again a moment
after it changes, so it follows Kalendar by itself. If the days that are shown
differ then, it takes all its events back and hands them over anew; if they do
not, Plasma hears nothing.

Plasma (6.8 at least) builds its calendar anew for every event a plugin takes
back (`eventRemoved()`), one after the other and slower each time: a few dozen
in a row freeze the whole shell while its memory grows without end. Events
are taken back by their identifier, so the plugin gives all of them the same
one. One signal then takes back everything, whatever changed. Asking for a
range of days never takes anything back, because Plasma has dropped what it
had by then. It never synchronizes: events from servers
are as fresh as Kalendar's last round, so Kalendar has to run now and then.

There may be two databases, one of a native Kalendar and one of the Flatpak.
The plugin takes the one that was written last.

## How it is installed

Plasma looks for plugins where the system has them, which takes an
administrator to write to. *Settings → Install Plugin* instead installs it
for the user (`ClockPlugin::install()`):

1. A copy of the plugin goes to
   `~/.local/lib/kalendar/plugins/plasmacalendarplugins/kalendarevents.so`.
2. `~/.config/plasma-workspace/env/kalendar-clock-plugin.sh` gets one line
   that adds `~/.local/lib/kalendar/plugins` to `QT_PLUGIN_PATH`. Plasma runs
   the scripts in that folder when it starts.

After logging out and in, the plugin is offered in the clock's settings under
*Calendar*. *Remove Plugin* deletes both files. At every start Kalendar
compares an installed copy with the plugin it came with and replaces it if
they differ (`refresh()`), so the plugin follows updates of the app.

The plugin that is copied is the one next to the executable in a build
folder, or in the plugin folder Kalendar was installed with. If that folder
is one the system's Qt searches anyway, the plugin is already installed with
the system and the settings say so instead of offering a button.

## From the Flatpak

The Flatpak builds the plugin too and may write to the two folders above.
Two things follow from that:

- **The sandbox is not complete.** An app that may write a script that runs
  at login can reach beyond its sandbox. It is the only way to install the
  plugin without an administrator.
- **The plugin is built against the Flatpak's libraries but loaded by the
  system's Plasma.** That works where the system's Qt and KDE Frameworks are
  at least as new as the Flatpak's, and where the system has KCalendarCore.
  Kalendar cannot tell from inside the sandbox whether Plasma accepted it.

Distributions can install the plugin with the system instead:
`cmake --install build --component plasmaplugin`.
