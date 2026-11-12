<!--
SPDX-FileCopyrightText: 2026 Kalendar contributors
SPDX-License-Identifier: CC0-1.0
-->

# Kalendar code documentation

How Kalendar is put together, for people who want to read or change the code.
The [README](../README.md) at the top of the repository says what Kalendar is
and how to install and build it; these pages go into the code.

| Page | What it covers |
| --- | --- |
| [Architecture](architecture.md) | The parts, how they talk to each other, where data is kept |
| [Synchronization](synchronization.md) | Events as calendar files, working without a network, conflicts, CalDAV, Google's Calendar API |
| [Accounts and signing in](accounts.md) | CalDAV passwords in KWallet, the Google accounts of the system's Online Accounts |
| [Interface](interface.md) | The QML files, how views get their events, settings, QML pitfalls |
| [The clock plugin](clock-plugin.md) | Events in the calendar of Plasma's clock, and how the plugin is installed |
| [Development](development.md) | Building, tests, running and debugging, conventions, how to add things |
| [Releasing](releasing.md) | Making a release, what the workflow does with it, the Flatpak repository and its signing key |

The headers under `src/` carry the reference documentation of each class as
comments. These pages explain how the classes fit together and why they are
the way they are; they do not repeat every member.

## The code at a glance

About 9,400 lines: C++20 for the logic, QML (Kirigami) for the interface.

```
src/
  core/           No GUI, unit-tested: database, CalDAV client, Google client, engine
  *.cpp, *.h      What only an app has: wallet, Online Accounts, the layer QML talks to
  qml/            The interface
  plasma/         The plugin for the calendar of Plasma's clock
autotests/        Tests, with a stand-in for Google's Calendar API
flatpak/          The Flatpak manifest and what publishes it
docs/             These pages
```
