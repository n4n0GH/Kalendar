<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# Architecture

Kalendar is one process. The interface is QML; everything else is a handful of
C++ objects created in `src/main.cpp`.

```
          ┌──────────────────────── QML (src/qml) ────────────────────────┐
          │ CalendarPage ─ MonthView ─ WeekView ─ dialogs ─ SettingsPage  │
          └───────────────┬───────────────────────────────┬───────────────┘
                          │                               │
                       Backend ── SecretStore ── KWallet  ClockPlugin
                          │   └── OnlineAccounts ── accounts.db, signond
                          ▼
                        Engine ──────────────── Database (SQLite)
                          │
            ┌─────────────┴─────────────┐
        DavClient                 GoogleClient
        (CalDAV servers)          (Google's Calendar API)

   Plasma's clock ── kalendarevents.so ── Engine (read-only) ── the same database
```

## The parts

| Class | File | Role |
| --- | --- | --- |
| `Database` | `src/core/database.*` | Accounts, calendars and events in SQLite. Nothing but storage. |
| `Engine` | `src/core/engine.*` | The heart: holds everything in memory, answers what happens when, applies changes, and keeps calendars in step with their servers. |
| `CalendarClient` | `src/core/davclient.h` | What the engine needs from a server, whichever language it speaks. |
| `DavClient` | `src/core/davclient.*` | CalDAV: finding an account's calendars, listing, fetching, storing and deleting events. |
| `GoogleClient` | `src/core/googleclient.*` | The same for Google's Calendar API, turning its events into calendar files and back. |
| `Backend` | `src/backend.*` | What QML talks to. A thin layer over the engine that adds what only an app has: signing in, settings, text for people. |
| `SecretStore` | `src/secretstore.*` | Passwords in KWallet. |
| `OnlineAccounts` | `src/onlineaccounts.*` | The Google accounts of the system's Online Accounts, and tokens for them. |
| `SingleInstance` | `src/singleinstance.*` | Keeps Kalendar to one process, and hands that one the files another was started with. |
| `ClockPlugin` | `src/clockplugin.*` | Installs the plugin for Plasma's clock for the user. |
| `KalendarEventsPlugin` | `src/plasma/` | The plugin itself, which runs inside Plasma. |

`main()` creates the engine, the secret store, the Online Accounts reader and
the backend, in that order, and hands the backend to QML through a static
`create()` that returns the existing instance (`QML_SINGLETON` with C++
ownership). `ClockPlugin` depends on nothing and is created by the QML engine
on first use.

## One Kalendar at a time

The engine keeps every event in memory, so two Kalendars on one database
would each know nothing of what the other changed. `main()` therefore first
takes the name `io.github.n4n0gh.kalendar` on the session bus
(`SingleInstance::claim()`). If it is taken, the files from the command line
go to the Kalendar that has it, which brings its window to the front and
offers to import them; the new process quits. The running one answers to
`org.freedesktop.Application` (`Activate`, `Open`, `ActivateAction`) at
`/io/github/n4n0gh/kalendar`. With `KALENDAR_TEST_MODE` set, none of this
happens.

## The core library

`src/core` builds as the static library `kalendarcore`. It has no GUI and
knows nothing of wallets or settings, so it can be tested without a display,
and it is what the clock plugin links to read the database. Signing in is
handed to it as a function: `Engine::setAuthProvider()` takes a callback that
is asked for the `Authorization` header of an account whenever one is needed.

It builds on KCalendarCore for everything about calendar files: parsing and
writing iCalendar, time zones, and working out when repeating events happen.

## Threads

The app has one, the main thread (the clock plugin has a second: see
[The clock plugin](clock-plugin.md)). Network requests, D-Bus calls and the wallet are
asynchronous through Qt's event loop; SQLite and the handling of calendar
files are not, and are kept short. The one thing that was not short, writing
hundreds of fetched events one by one, is done in a single transaction (see
[Synchronization](synchronization.md#writing-to-the-database)).

## Where data is kept

| What | Where |
| --- | --- |
| Accounts, calendars, events | `calendars.sqlite` in the app's data folder: `~/.local/share/kalendar/`, or `~/.var/app/io.github.n4n0gh.kalendar/data/kalendar/` for the Flatpak |
| Passwords of CalDAV accounts | KWallet, folder `Kalendar`, one entry per account id |
| Google sign-ins | Not kept by Kalendar at all: the system's Online Accounts has them |
| Settings | `kalendar.conf` in the config folder (`QSettings`, and QML's `Settings` for the window) |
| The installed clock plugin | `~/.local/lib/kalendar/plugins/` and one line in `~/.config/plasma-workspace/env/` |

A native build and the Flatpak keep their data apart. The clock plugin reads
whichever database was written last.

### The database

Three tables, created on first use:

```
accounts     id, type (0 local, 1 CalDAV, 2 Google), name, url, user
collections  id, account, url, name, color, ctag, enabled, readonly
items        collection, href, etag, ics, dirty        key: (collection, href)
```

An item is one calendar file: an event together with the exceptions to its
repetition, exactly as iCalendar text in `ics`. Nothing about an event is
spread over columns; the engine parses the files at start and keeps the
result in memory. `dirty` says whether the item still has to be sent to its
server (see [Synchronization](synchronization.md)).

The journal is in WAL mode, which lets the clock plugin read while Kalendar
writes.

## Online services

Kalendar talks to the servers of the accounts the user added and to nothing
else. It has no telemetry and fetches nothing by itself: notes of events are
shown with pictures and scripts removed for that reason (see
[Interface](interface.md#notes)).
