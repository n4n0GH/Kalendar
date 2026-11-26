// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"

#include <QDBusMessage>
#include <QObject>

#include <functional>

class QTimer;

/// The calendars the system has besides Kalendar's own: those of Akonadi,
/// which KOrganizer, Merkuro and the calendar of Plasma's clock show.
///
/// Akonadi hands out what it keeps through its libraries only, and the
/// runtime of the Flatpak has none of them. What it shows from calendar
/// files can be had without, though: over D-Bus it says which agents it
/// runs, and an agent for a calendar file (akonadi_ical_resource), or for a
/// folder with a file for each entry (akonadi_icaldir_resource), says where
/// its file is. Those files are what is found here, each with the name
/// Akonadi shows it under; the engine reads them. Calendars that Akonadi
/// fetches from servers, or makes up from the address book, are not found.
///
/// Akonadi is never started to be asked. While it does not run, nothing is
/// found, and what was found before stays as it is; where there is no
/// Akonadi at all, it is found that there are no calendars. Akonadi says
/// when its agents change, and that is listened to.
class SystemCalendars : public QObject
{
    Q_OBJECT

public:
    /// With @p testMode there is no system to ask: the calendars are those
    /// named in KALENDAR_SYSTEM_CALENDARS ("To-Dos=/path/to-dos.ics,…").
    explicit SystemCalendars(bool testMode, QObject *parent = nullptr);

    /// Asks anew. The answer arrives as found(), unless Akonadi does not run.
    void refresh();

Q_SIGNALS:
    /// The calendar files there are. With @p complete false, an agent did
    /// not say where its file is, so there may be more than these.
    void found(const QList<CalendarInfo> &calendars, bool complete);

private Q_SLOTS:
    /// Akonadi got, lost or renamed an agent, or was started.
    void agentsChanged();

private:
    struct Agent {
        QString instance;
        /// What its settings are called on the bus.
        QString interface;
    };
    using Reply = std::function<void(const QDBusMessage &)>;

    void call(const QString &service, const QString &path, const QString &interface, const QString &method, const QVariantList &arguments, const Reply &then,
              int timeout);
    void look();
    /// Asks the agents one after the other, and gives the result when all are asked.
    void ask(quint64 round, QList<Agent> agents, const QList<CalendarInfo> &calendars, bool complete);
    void finish(const QList<CalendarInfo> &calendars, bool complete);

    const bool m_testMode;
    /// Goes up with every question: answers to an earlier one do not count.
    quint64 m_round = 0;
    /// An agent that is new has to be set up before it can be asked.
    QTimer *const m_soon;
    /// For another try after an agent did not answer, a few times.
    QTimer *const m_again;
    int m_tries = 0;
};
