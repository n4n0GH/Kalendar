<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# The calendars of the system

KDE's other calendar programs (KOrganizer, Merkuro, the calendar of Plasma's
clock) share their calendars through Akonadi. Kalendar keeps its own, but
shows Akonadi's next to them where it can: as an account of its own, *System
Calendars*, that can only be read. That is how a calendar another program
hands to the system turns up in Kalendar.

## What is shown, and what is not

Akonadi hands out what it keeps through its libraries only, and the KDE
runtime of the Flatpak has none of them. What it shows *from calendar files*
can be had without, though, because the files are there to read:

| Akonadi's calendar | Agent | Shown |
| --- | --- | --- |
| A calendar file (`.ics`): KOrganizer's *Personal Calendar*, any file a program or the user added as a calendar | `akonadi_ical_resource` | Yes |
| A folder with a file for each entry | `akonadi_icaldir_resource` | Yes |
| Calendars Akonadi fetches from a server (CalDAV, Google, Exchange, …) | others | No: add the account to Kalendar instead |
| *Birthdays & Anniversaries*, which Akonadi makes up from the address book | `akonadi_birthdays_resource` | No |

Kalendar asks Akonadi and reads the files. It writes to neither: the
calendars are another program's, and are marked as ones that cannot be
changed, whatever Akonadi lets others do with them.

## Asking Akonadi: SystemCalendars

`SystemCalendars` (`src/systemcalendars.*`) finds the files, over D-Bus on the
session bus, without anything of Akonadi linked:

1. `NameHasOwner` on the bus says whether Akonadi runs
   (`org.freedesktop.Akonadi.Control`).
2. `agentInstances` on its `/AgentManager` lists the agents. An agent is
   called by its kind and a number (`akonadi_ical_resource_1`), which is how
   the two kinds above are picked.
3. Each of those is asked for `path` on its own service
   (`org.freedesktop.Akonadi.Resource.<agent>`, object `/Settings`, interface
   `org.kde.Akonadi.ICal.Settings` or `org.kde.Akonadi.ICalDirectory.Settings`),
   and the control for the name the agent is shown under
   (`agentInstanceName`).

The result goes out as the signal `found()`, which `Backend` connects to
`Engine::setSystemCalendars()`. The rules it keeps to:

- **Akonadi is never started to be asked.** While it does not run, nothing
  is found and Kalendar keeps what it found before. Where the bus cannot
  start it either, there is no Akonadi, and it is found that there are no
  calendars: the account goes.
- **An agent that does not answer is not an agent that is gone.** `found()`
  then says that the list is not complete, and the engine keeps the
  calendars that are not in it. It is tried again a few times, 20 seconds
  apart, because an Akonadi that just started has agents that are not up
  yet.
- **An agent without a file is left out**: one that was just made and is
  not set up yet. Akonadi shows nothing for it either.
- **Changes are heard of.** Akonadi signals when an agent is added, removed
  or renamed, and the bus when Akonadi starts; each has Kalendar ask again a
  second later. `Backend::sync()` asks too: at start, every 15 minutes, and
  on *Refresh*.
- Every call is asynchronous, and the answers to a question that has been
  asked anew since are dropped (`m_round`).
- `AKONADI_INSTANCE` is honoured: Akonadi's services end in it. That is how
  this is tried without touching the Akonadi of the desktop, and how the
  test does it.

## Reading the files: Engine

`Engine::setSystemCalendars()` takes the list and makes it the calendars of
one account (`Account::System`, id `system`), the same way a server's list
of calendars is taken over (`reconcile()`): new ones are added, names
follow, ones that are gone go with their events. With no calendars, the
account goes too. Whether a calendar is shown is the user's choice in the
sidebar and is kept.

`readSystemCalendar()` reads one. The address of the calendar is its file
or folder; its `ctag` is a hash of what was read, so that a file that is as
it was costs nothing but the hashing. Otherwise:

- The file is split into items the way servers hand them over: one per UID,
  an event with the exceptions to its repetition. Their address is
  `system:` plus the UID. Items are compared with what is there, so only
  entries that really differ are written to the database and make the views
  lay themselves out again.
- **To-dos** that are open and due become events at the time they are due
  (`eventOf()`): for whole days if the to-do names a day, at the time of day
  if it names one. They carry the property `X-KALENDAR-TODO`, which
  `toVariant()` hands to the interface as `todo`. To-dos that are done, or
  due on no day, have no place in a calendar and are left out. This is done
  for the calendars of the system only: on a server, a to-do turned into an
  event would be sent back as one.
- A file that is not there is a calendar nothing was put into. A file that
  is empty is one that is being written right now, and is left for the next
  look. A file that cannot be read, or holds no calendar, is an error on the
  account, and what was read before stays.

The files are watched (`QFileSystemWatcher`): the file itself for being
written to, and its folder for being replaced by another, which is how most
programs save. Changes are waited out for a moment and then read as one.
`syncAll()` reads them as well, in case a change was missed. In a folder of
files, a change *within* a file is only seen that way; files that come and
go are seen at once.

Everything is in the database like any other calendar, so it is there at the
next start before Akonadi is asked, and without Akonadi running.

## The clock plugin leaves them out

Plasma shows Akonadi's calendars in the clock's calendar with a plugin of
its own (*PIM Events Plugin*, from kdepim-addons). Kalendar's plugin
therefore skips the calendars of the system: with both plugins switched on,
they would be there twice.

## In the Flatpak

Four permissions in the manifest are for this:

- `--talk-name=org.freedesktop.Akonadi.Control` and
  `--talk-name=org.freedesktop.Akonadi.Resource.*`, to ask.
- `--filesystem=home:ro`, to read the files, which may be anywhere in the
  home folder, and `--filesystem=~/.var/app:ro`, because other Flatpaks keep
  theirs in their data folders, which `home` leaves out.

A calendar file outside the home folder is not seen from the sandbox, and
looks like a calendar without entries. `flatpak override --user
--filesystem=/the/folder:ro io.github.n4n0gh.kalendar` lets it be read.

## Trying it

- **Without Akonadi.** In test mode there is no system to ask:
  `KALENDAR_SYSTEM_CALENDARS="To-Dos=/path/to-dos.ics,Other=/path/other.ics"`
  names the calendars. Without it there are none, and an account left over
  from an earlier try goes.
- **With the real Akonadi, and data of its own.** Kalendar only asks, so the
  desktop's Akonadi can be used; a data folder of its own keeps the real
  calendars out of it:

  ```sh
  T=$(mktemp -d)
  XDG_DATA_HOME=$T/data XDG_CONFIG_HOME=$T/config build/bin/kalendar
  ```

- **What Akonadi has**, to compare:
  `qdbus6 org.freedesktop.Akonadi.Control /AgentManager agentInstances`, and
  `qdbus6 org.freedesktop.Akonadi.Resource.akonadi_ical_resource_0 /Settings path`.
- `systemcalendarstest` puts a stand-in for Akonadi on the session bus, under
  a name of its own (`AKONADI_INSTANCE`); `enginetest` covers the files.
