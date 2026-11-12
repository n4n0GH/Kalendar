<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# Development

## Building

Needed: Qt 6.6 or newer, KDE Frameworks 6.8 or newer (Kirigami, KCalendarCore,
KI18n, KCoreAddons, KColorScheme, KWallet, and KDeclarative for the clock
plugin), and extra-cmake-modules. The package names for openSUSE are in the
[README](../README.md#building).

```sh
cmake -B build -G Ninja
cmake --build build
build/bin/kalendar
```

Without KDeclarative's development files the clock plugin is left out and
everything else builds. Development files that cannot be installed with the
system can be unpacked anywhere and named with `-DCMAKE_PREFIX_PATH=…`.

A build run from its folder uses the same data as an installed one
(`~/.local/share/kalendar`), and not the Flatpak's.

## Flatpak

```sh
flatpak-builder --force-clean build-flatpak flatpak/io.github.n4n0gh.kalendar.yml
flatpak-builder --run build-flatpak flatpak/io.github.n4n0gh.kalendar.yml kalendar
```

builds it and runs the result without installing. Everything Kalendar builds
on is in the KDE runtime, so Kalendar is all that is built. Add
`--user --install` to the first line to install it, which replaces a Kalendar
installed from the repository.

The permissions in the manifest each carry a comment saying what they are
for. How releases are made from it is in [Releasing](releasing.md).

## Tests

```sh
ctest --test-dir build --output-on-failure
```

| Test | What it covers | Needs |
| --- | --- | --- |
| `appstreamtest` | The metainfo file is valid | |
| `davparsertest` | Reading CalDAV answers | |
| `enginetest` | The calendar on this system: events, repetition, layouts, undo | |
| `synctest` | Two Kalendars sharing an account on a real CalDAV server | Radicale |
| `googletest` | Google's events to calendar files and back; a full round with the stand-in for Google | `python3` |
| `onlineaccountstest` | Reading the system's account list | |
| `clockplugintest` | Installing, refreshing and removing the clock plugin, in a temporary folder | |
| `backendtest` | Adding and removing accounts, times of day, notes | Radicale, `python3` |
| `plasmaplugintest` | The plugin, loaded as Plasma would | KDeclarative |

Tests that need something that is missing skip themselves.

**Radicale** is a small CalDAV server the tests start themselves.
`KALENDAR_RADICALE` names the Python that has it:

```sh
python3 -m venv /tmp/radicale && /tmp/radicale/bin/pip install radicale
KALENDAR_RADICALE=/tmp/radicale/bin/python ctest --test-dir build
```

**`autotests/fakegoogle.py`** is a stand-in for Google's Calendar API with
what Kalendar uses of it: tokens, etags, calendars that cannot be changed,
changed appearances of repeating events as events of their own. `PUT
/control/<calendar>/<event>` stores an event exactly as given, as if changed
elsewhere; events are listed two at a time so that paging is tried; a second
argument makes listing slow, to look at what Kalendar shows meanwhile.

**The real Google** is only read, and only when asked:
`KALENDAR_LIVE_GOOGLE=1 build/bin/onlineaccountstest readsTheRealThing`
fetches a token for the first Google account of the system's Online Accounts
and synchronizes it into a temporary database. It prints how many calendars
and events it found. Nothing is written to Google.

Qt's time zone and the user's language change what some tests see; they are
written to pass in any, and worth running with `TZ=America/New_York` and
`LC_ALL=de_DE.UTF-8` after touching dates or times.

## Running and debugging

- **Seeing messages.** Without a terminal Qt logs to the journal. Set
  `QT_FORCE_STDERR_LOGGING=1` to get QML errors where you are looking.
- **Without touching real data.** `KALENDAR_TEST_MODE=1` keeps data and
  settings under `~/.qttest`, keeps passwords in memory, takes Online
  Accounts from `KALENDAR_ONLINE_ACCOUNTS`, and installs the clock plugin
  under `~/.qttest`. `KALENDAR_DEMO=1` adds a few events around today.
- **Without a display.** In test mode, `KALENDAR_EVAL` runs JavaScript in the
  scope of `Main.qml`, and `KALENDAR_SCREENSHOT` saves a picture and quits:

  ```sh
  KALENDAR_TEST_MODE=1 KALENDAR_DEMO=1 QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=kde \
      KALENDAR_EVAL='root.viewMode = "week"' KALENDAR_SCREENSHOT=week.png build/bin/kalendar
  ```

  The pictures in `docs/` are made this way. The colours come from
  `~/.qttest/config/kdeglobals`: a copy of
  `/usr/share/color-schemes/BreezeDark.colors` with `[Icons] Theme=breeze-dark`
  appended gives the dark one.
- **With the stand-in for Google.** Start `python3 autotests/fakegoogle.py
  18765`, and Kalendar in test mode with
  `KALENDAR_GOOGLE_API=http://127.0.0.1:18765/calendar/v3/` and
  `KALENDAR_ONLINE_ACCOUNTS="1=Work"`.

Messages that are not Kalendar's: "Could not create delegate for
ToolBarLayout", "StackView has detected conflicting anchors" and "Only
binding to one of multiple key bindings" come from Kirigami and the desktop
style, also in other apps, and have no visible effect.

## Conventions

- **C++**: C++20 with KDE's compiler settings, which are strict: string
  literals need `u"…"_s` or `"…"_L1`, signals are emitted with `Q_EMIT`,
  `Q_SIGNALS`/`Q_SLOTS` instead of the keywords.
- **Comments** say why, or what is not obvious; class comments in headers are
  the reference documentation.
- **Strings** for the user go through `i18nc` with a context. `Messages.sh`
  extracts them; there are no translations yet.
- **Licensing** follows REUSE: every file starts with SPDX lines. Code is
  GPL-3.0-or-later, build files BSD-2-Clause, documentation, the manifest
  and icons CC0-1.0; the texts are in `LICENSES/`.
- **The core stays free of the app.** `src/core` has no GUI, no wallet and no
  settings, which is what lets it be tested alone and linked into the clock
  plugin.
- **Test hooks stay out of users' way.** What exists for tests is switched by
  environment variables that start with `KALENDAR_`; those that drive the
  interface or stand in for the system's accounts only work in test mode.
- **QML**: see the pitfalls in [Interface](interface.md#qml-conventions-that-bite).

## How to …

**… add a setting.** If views depend on it, add a property with a signal to
`Backend`, kept in `QSettings` (as `use24Hour` is); if only the window does,
a property of `Main.qml` with an alias in its `Settings`. In
`SettingsPage.qml`, add a property of the page that starts as the stored
value, include it in `changed` and `apply()`, and bind the control to the
page's property, not to the stored one. Radio buttons need a `ButtonGroup`.

**… add a field to events.** Read and write it in `Engine::saveEvent()` and
`toVariant()`, add it to `EventDialog.qml` and `EventViewDialog.qml`. CalDAV
carries it by itself, since the whole calendar file travels. For Google, add
it to `toEvent()` and `toJson()` in `googleclient.cpp`, and to the test.

**… add a view.** Add a mode to `viewMode`, an action to `CalendarPage.qml`,
what `step()` and the title do in that mode, and the component to its
`Loader`. Ask `Backend` for a layout rather than computing one in QML; if
none fits, add a function to `Engine` beside `weekLayout()` and test it in
`enginetest`.

**… add another kind of server.** Derive from `CalendarClient`, add a value to
`Account::Type`, create the client in `Engine::clientFor()`, say what
address new events get where `saveEvent()` does, and sign in for it in
`Backend::authorize()`. Answer with HTTP statuses, or with what stands for
them: `Engine::pushNext()` decides by them what to do when the server says
no.

**… change the database.** There are no migrations yet: the tables are
created `IF NOT EXISTS`. A new column needs an `ALTER TABLE` for files that
exist, in `Database::open()`, and must not break `openReadOnly()`, with which
an older clock plugin may read a newer file.

**… make new pictures for the README.** See "Without a display" above; the
window is 1080 by 684.

## Known limitations

They are listed in the [README](../README.md#not-there-yet). The ones that
shape the code most: one appearance of a repeating event can be deleted but
not changed here (changed ones from elsewhere are shown, and can be edited);
Google's calendars are listed in full at every round; reminders, guests and
tasks are not read at all, though on Google they survive Kalendar's changes.
