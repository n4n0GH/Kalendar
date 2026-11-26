<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# The calendars of the system

KDE's other calendar programs (KOrganizer, Merkuro, the calendar of Plasma's
clock) share their calendars through Akonadi. Kalendar keeps its own, and
meets Akonadi both ways:

- It **shows Akonadi's calendars** next to its own where it can: as an
  account of its own, *System Calendars*. That is how a calendar another
  program hands to the system turns up in Kalendar. Those that Akonadi lets
  be changed can be changed here.
- It **hands its own events to Akonadi**, if the user wants that, as one
  calendar that the other programs show. That is how Kalendar's events get
  into the calendar of Plasma's clock.

Both are done with calendar files and a few calls on the session bus, and
without linking anything of Akonadi.

## What is shown, and what is not

Akonadi hands out what it keeps through its libraries only, and the KDE
runtime of the Flatpak has none of them. What it shows *from calendar files*
can be had without, though, because the files are there to read:

| Akonadi's calendar | Agent | Shown |
| --- | --- | --- |
| A calendar file (`.ics`): KOrganizer's *Personal Calendar*, any file a program or the user added as a calendar | `akonadi_ical_resource` | Yes, and changed if Akonadi lets it be |
| A folder with a file for each entry | `akonadi_icaldir_resource` | Yes, to look at |
| Calendars Akonadi fetches from a server (CalDAV, Google, Exchange, …) | others | No: add the account to Kalendar instead |
| *Birthdays & Anniversaries*, which Akonadi makes up from the address book | `akonadi_birthdays_resource` | No |

Kalendar asks Akonadi and reads the files. Akonadi itself is told nothing
about them; a calendar that it lets be changed is changed by writing to its
file, which Akonadi's agent notices (see *Writing to the files* below).

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
4. An agent for one file is also asked `readOnly` and `monitorFile`. Its
   calendar may be changed if the first is false and the second true: an
   agent that does not look at its file again when another program wrote to
   it would write what it remembers over the change. A folder of files is
   only read.

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
- The file in which Kalendar hands its own events to Akonadi (below) is
  never found (`leaveOut()`): they would come back as a calendar of the
  system otherwise.

## Reading the files: Engine

`Engine::setSystemCalendars()` takes the list and makes it the calendars of
one account (`Account::System`, id `system`), the same way a server's list
of calendars is taken over (`reconcile()`): new ones are added, names
follow, ones that are gone go with their events. With no calendars, the
account goes too. Whether a calendar is shown is the user's choice in the
sidebar and is kept. A calendar can be changed if Akonadi says so *and* its
file can be written (`canWrite()`), which it cannot from a sandbox that may
only read it; the sidebar puts a lock on the others.

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

## Writing to the files

Events are added to, changed in and deleted from a calendar of the system
like in any other: `saveEvent()` and `deleteEvent()` mark the item as changed
or deleted, and `push()` sends it on. For this account, sending on is
`writeSystemCalendar()`:

1. **The file is read as it is right now.** Another program may have written
   to it a moment ago, and Kalendar may not have looked yet.
2. **Only the events changed here are touched**, and the file is handled as
   text for that (`scan()`): the events with the UIDs in question are taken
   out, the changed ones are put in before the calendar closes, with the
   time zones they need and the file does not have. Everything else stays
   as it is written: other events, to-dos, journals, properties nobody here
   knows. A UID that is written over two lines is found too.
3. **It is written beside the file and then put in its place**, so that
   nobody reads half a file.
4. The file is read again the usual way, which takes over what else changed
   in it meanwhile.

What cannot be written (the folder is locked, the disk is full) waits: the
items stay as changed, the account says why, and `syncAll()` tries again.
Reading leaves items alone that wait to be written. If a calendar can no
longer be changed by the time a change is to be written, the change is taken
back with a notice, as for a server that refuses one.

**To-dos are only shown.** The events that stand for them are refused by
`saveEvent()` and `deleteEvent()`, and handed to the interface as read only,
in a calendar that can be changed too.

**What Akonadi does with it.** Its agent watches the file. When the file
changed, the agent reads it again, and KOrganizer and the others show the
change; tried with Akonadi 26.08, that takes a few seconds. As a precaution
the agent first writes down the calendar as it had it, as a file of its own
(`~/.local/share/akonadi_ical_resource_<n>/<name>-1`, `-2`, …): **one such
copy for every change made from outside**, which nobody clears away. That is
Akonadi's doing and cannot be switched off from here. The agent may also
write the file anew in its own order afterwards, which Kalendar reads like
any other change.

## Handing Kalendar's events to the system

*Settings → Add to the System's Calendars* makes Kalendar's events one
calendar of Akonadi, named *Kalendar*. Two parts do it.

### The file: Engine

`Engine::setExportFile()` names a calendar file, `kalendar.ics` next to the
database, that the engine keeps from then on (`exportedCalendar()`,
`writeExport()`):

- It holds the events of the calendars that are **shown** in Kalendar. The
  calendars of the system are left out: Akonadi has those already.
- The events are taken from the stored items **as they are written**
  (`takeParts()`), not parsed and written anew: that is quick, and keeps
  what Kalendar itself knows nothing of, such as reminders and guests. A
  time zone that several items bring along goes in once.
- An event that is in two calendars goes in once: other programs tell events
  apart by their UID.
- The file is written a second after events change, beside itself and then
  in its own place, so that nobody reads half a file; and only if what it is
  to hold differs from what it holds, so that Akonadi is not made to read
  the same again. The items go in sorted, which keeps the file the same for
  the same events.

One calendar for all of them, rather than one per calendar of Kalendar's:
Akonadi runs a process for every calendar file it shows.

### The calendar: SystemExport

`SystemExport` (`src/systemexport.*`) has Akonadi show the file: it has an
agent of the kind `akonadi_ical_resource` made (`createAgentInstance` on
`/AgentManager`), tells it the file (`setPath` on the agent's `/Settings`),
sets it to read only and to watch the file, names it, and has it removed
again (`removeAgentInstance`).

- Whether the events are shown is asked, not remembered: the agents for
  calendar files are asked for their path. It stays true when the user
  removes the calendar in KOrganizer; the settings then offer to add it
  again.
- Akonadi is started only when the user has the calendar added, never to be
  asked or to be told to forget it. While it does not run, the state of the
  last time counts (`SystemExport/shown` in the settings file), and a
  calendar that is to go is noted (`SystemExport/forget`) and removed the
  next time Akonadi is found running.
- `SystemExport/enabled` says that the file is wanted; with it set, the
  engine is given the file at every start.
- An agent that was just made answers after a moment; `configure()` tries
  until it does. If adding fails, nothing of it is left behind: no agent, no
  file.
- Removing takes the agent away while the file is still there to be found
  by, and then the file.

### In Plasma's clock

Plasma shows Akonadi's calendars in the calendar of the clock with a plugin
of its own, *PIM Events Plugin* from kdepim-addons, which is switched on in
the clock's settings under *Calendar*. The settings page says so once the
events are handed over.

Kalendar used to bring a plugin of its own for the clock, built into the
system's Plasma from the app's side, which took a login script to install
and broke where the system's libraries were older than the Flatpak's.
`SystemExport::retireClockPlugin()` removes what earlier versions put
into the user's folders, at every start (`main()`), and Kalendar says so
once when it did.

## In the Flatpak

These permissions in the manifest are for this:

- `--talk-name=org.freedesktop.Akonadi.Control`,
  `--talk-name=org.freedesktop.Akonadi.Resource.*` and
  `--talk-name=org.freedesktop.Akonadi.Agent.*`, to ask Akonadi and to have
  it show Kalendar's file.
- `--filesystem=home`, to read the files and to write to those that may be
  changed, wherever in the home folder they are, and
  `--filesystem=~/.var/app:ro`, because other Flatpaks keep theirs in their
  data folders, which `home` leaves out. Those are only read, so calendars
  kept there carry the lock.

A calendar file outside the home folder is not seen from the sandbox, and
looks like a calendar without entries. `flatpak override --user
--filesystem=/the/folder io.github.n4n0gh.kalendar` lets it be read and
written. To keep Kalendar from writing anywhere in the home folder, `flatpak
override --user --filesystem=home:ro io.github.n4n0gh.kalendar` makes every
calendar of the system one to look at.

Kalendar's own file is in the Flatpak's data folder, which has the same
path inside the sandbox and outside, so Akonadi finds it where Kalendar
says it is.

## Trying it

- **Without Akonadi.** In test mode there is no system to ask:
  `KALENDAR_SYSTEM_CALENDARS="To-Dos=/path/to-dos.ics,Other=rw:/path/other.ics"`
  names the calendars; `rw:` before the path makes one that may be changed. Without it there are none, and an account left over
  from an earlier try goes.
- **With the real Akonadi, and data of its own.** Kalendar only asks, so the
  desktop's Akonadi can be used; a data folder of its own keeps the real
  calendars out of it:

  ```sh
  T=$(mktemp -d)
  XDG_DATA_HOME=$T/data XDG_CONFIG_HOME=$T/config build/bin/kalendar
  ```

- **With an Akonadi of its own**, to try handing events over without
  touching the desktop's. A private session bus keeps the two apart on the
  bus, but an Akonadi started in it would still open the database of the
  real one, so the session gets a home of its own as well:

  ```sh
  T=$HOME/.cache/kalendar-try              # not /tmp: the home must not be on a RAM disk
  mkdir -p $T/home && mkdir -p -m 700 $T/run
  env HOME=$T/home XDG_DATA_HOME=$T/home/.local/share XDG_CONFIG_HOME=$T/home/.config \
      XDG_STATE_HOME=$T/home/.local/state XDG_CACHE_HOME=$T/home/.cache \
      XDG_RUNTIME_DIR=$T/run AKONADI_INSTANCE=try \
      dbus-run-session -- sh -c 'akonadictl --instance try start >/dev/null 2>&1 & sleep 15; \
          KALENDAR_TEST_MODE=1 KALENDAR_DEMO=1 build/bin/kalendar; akonadictl --instance try stop'
  ```

  In test mode Kalendar talks to no Akonadi but one that `AKONADI_INSTANCE`
  names. Without a display, `KALENDAR_EVAL='SystemExport.add()'` presses the
  button. Inside the session, `qdbus6 org.freedesktop.Akonadi.Control.try
  /AgentManager agentInstances` lists the agents; what Akonadi read from the
  file is in `pimitemtable` of `akonadi.db` below
  `$T/home/.local/share/akonadi`. `akonadictl start` keeps its output open:
  do not wait on a pipe behind it.
- **What Akonadi has**, to compare:
  `qdbus6 org.freedesktop.Akonadi.Control /AgentManager agentInstances`, and
  `qdbus6 org.freedesktop.Akonadi.Resource.akonadi_ical_resource_0 /Settings path`.
- `systemcalendarstest` and `systemexporttest` put a stand-in for Akonadi on
  the session bus, under a name of its own (`AKONADI_INSTANCE`); `enginetest`
  covers the files, both ways.
