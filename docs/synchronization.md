<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# Synchronization

Everything here is in `Engine` (`src/core/engine.*`) and the two clients it
drives.

## The idea

A change made in Kalendar is applied at once, to memory and to the database,
and sent to the server afterwards. The calendar therefore works the same with
and without a network; what could not be sent waits for the next
synchronization. The server always has the last word on what an event looks
like once both sides have spoken.

## Events as calendar files

Whatever the server speaks, the engine sees *items*: one calendar file
(iCalendar text) with an address (`href`) and a version (`etag`) that changes
whenever the file does. A file holds one event and, for an event that
repeats, the appearances of it that were changed ("exceptions", events with a
`RECURRENCE-ID`).

For each calendar the engine keeps a `State`: the items by address, a
KCalendarCore `MemoryCalendar` with the events of the calendars that are
shown, and which address an event's UID belongs to. `setItem()` and
`dropItem()` are the only places that change these, and they keep memory and
database in step.

Each item is in one of three conditions (`ItemRecord::Dirty`):

| Value | Meaning |
| --- | --- |
| `Clean` | The same here and on the server, as far as is known |
| `Changed` | Changed or made here, not yet sent |
| `Deleted` | Deleted here, not yet deleted on the server; no longer shown |

A calendar on this system only (`Account::Local`) has no server: its items
become clean, or are dropped, right away.

## One round

`Engine::syncAccount()` does this for one account; `Backend` calls
`syncAll()` at start, every 15 minutes, and on *Refresh*.

1. **Sign in.** The auth provider is asked for the `Authorization` header.
2. **List the calendars** and take over what changed (`reconcile()`): new
   calendars are added, names and colours follow the server, calendars that
   are gone from the server go here too, with their events.
3. For each calendar, **send first** (`push()`): everything that is not clean
   goes to the server, one item after the other.
4. Then **fetch** (`pull()`): the server lists address and version of every
   event. What is new or has another version is fetched; what is no longer
   listed is dropped. Items that are not clean are left alone, so that a
   change waiting to be sent is never overwritten by a fetch.

CalDAV servers report a marker (`ctag`) that changes when anything in a
calendar does. When it is the same as last time and nothing was sent, step 4
is skipped. Google has no such marker, so its calendars are listed in full
every time (see [below](#googles-calendar-api)).

`Engine::syncProgress()` counts calendars done against calendars to do, for
the bar in the sidebar.

### Writing to the database

Fetched events are written inside one transaction per calendar
(`Database::begin()`/`commit()` around the loops in `pull()`). Written one by
one, each waits for the disk: a first synchronization of a few hundred events
took 13 seconds that way and blocked the interface; together it takes a
fraction of a second.

## When the server says no

`pushNext()` sorts the answers:

| Answer | What happens |
| --- | --- |
| 412 or 409 (changed elsewhere meanwhile) | The server's version is fetched and kept; a notice says so |
| 403 (calendar cannot be changed) | The same, with a notice that the change was taken back |
| No answer, 401, or 5xx | The server cannot be used right now: everything stays for the next round, and the account shows the error |
| Anything else | This one item is skipped until the next round |
| 404 or 410 on a deletion | Counts as done |

"The server wins" is deliberate. Merging two versions of an event needs a
person to decide, and losing an edit visibly is better than a silent mix.

## Deleting and undo

Deleting does not ask; it can be taken back for a few seconds
(`Engine::setUndoPeriod()`, 10 seconds by default). Until then a deleted item
is marked `Deleted` and *held*: no longer shown, not yet sent. `undo()` puts
it back as if nothing had happened. After the period the deletion is sent;
`undo()` after that sends the old version again as a change.

Deleting one appearance of a repeating event is not a deletion of the item
but a change to it: the event gets an `EXDATE`.

If Kalendar quits while a deletion is held, the item is still marked in the
database, and is sent at the next start or synchronization.

## What is asked of a server

`CalendarClient` (in `davclient.h`) is the whole of it:

| Function | Meaning |
| --- | --- |
| `listCalendars(home)` | The calendars of the account |
| `listEtags(collection)` | Address and version of every event |
| `fetch(collection, hrefs)` | The calendar files at these addresses |
| `put(url, ics, etag)` | Store a file. Without an etag it must be new; with one, the server's version must still be that |
| `remove(url, etag)` | Delete it, on the same condition |
| `etagOf(url)` | The current version of one event |

Answers come back as `DavReply`, whichever client it is; its `status` is an
HTTP status, which is what the table above looks at.

## CalDAV

`DavClient` speaks CalDAV (RFC 4791) directly on `QNetworkAccessManager`.

- **Finding the calendars** (`discover()`): from a server name alone it tries
  `/.well-known/caldav`, then the address as typed; asks for the user
  (`current-user-principal`), then for where that user's calendars are
  (`calendar-home-set`), then lists that folder. Only collections that take
  events (`VEVENT`) count; task lists are left out.
- **Redirections** are followed by hand, because Qt would turn a redirected
  `PROPFIND` into a `GET`. The password is only sent to hosts of the site it
  was typed for.
- **Listing** is a `calendar-query` report for etags; **fetching** is a
  `calendar-multiget` report, 50 events at a time.
- **Read-only** calendars are told by the `current-user-privilege-set`.

## Google's Calendar API

Google accounts that come from the system's Online Accounts may use Google's
Calendar API (version 3) but not Google's CalDAV, which answers "access not
configured" to them. `GoogleClient` therefore speaks the API, and makes its
events look like calendar files to the engine.

- **Addresses.** A calendar is `…/calendars/<id>/events`; an event's address
  is that plus its id. New events get an id made from their UID
  (`hrefFor()`), since Google takes only digits and the letters a to v.
- **From Google to a calendar file** (`toItems()`). Google lists the changed
  appearances of a repeating event as events of their own that name the event
  they belong to. They are put into the file of that event, as exceptions; a
  cancelled appearance becomes an `EXDATE`. The repeat rules arrive as lines
  of a calendar file (`RRULE:…`) and are read as such. Whole-day events end
  on "the day after the last" at Google and on the last day in KCalendarCore.
- **Versions.** The etag of an item is the event's own etag, followed by a
  bar and a hash of its exceptions' etags if it has any, so that a change to
  an appearance counts as a change to the file. What comes before the bar is
  what Google is asked to compare (`If-Match`).
- **From a calendar file to Google** (`toJson()`, `put()`). New events are
  `POST`ed; existing ones are `PATCH`ed, which changes only the fields that
  are sent, so that what Kalendar knows nothing of (guests, reminders,
  colours, video calls) stays as it is. An exception that was changed here is
  patched by its own id first; which ones were changed is told by a stamp
  kept with each when it was fetched (`X-KDE-KALENDAR-GOOGLE-STAMP`).
- **Listing is fetching.** Google sends the events along when it lists them,
  so `listEtags()` keeps what it saw and `fetch()` serves from that.
- **Too many requests** (429, or 403 with a reason about limits) are turned
  into a 503, so that the engine waits instead of taking the change back.

Not done: Google's incremental listing (`syncToken`). Every round lists each
calendar in full, which is a request or two per calendar.

`autotests/fakegoogle.py` is a stand-in for the API with what Kalendar uses of
it; see [Development](development.md#tests).
