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

It watches the database file and reads it again a moment after it changes, so
it follows Kalendar by itself. It never synchronizes: events from servers
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
