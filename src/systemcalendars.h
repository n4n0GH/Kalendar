// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"

#include <QDBusMessage>
#include <QHash>
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
/// Akonadi shows it under and whether Akonadi lets it be changed; the engine
/// reads them, and writes to those. Calendars that Akonadi
/// fetches from servers, or makes up from the address book, are not found.
///
/// Writing to a calendar's file has Akonadi's agent keep a copy of the
/// calendar as it was, every time. The old ones of the files Kalendar wrote
/// to are cleared away here, since the agents are known here.
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
    /// named in KALENDAR_SYSTEM_CALENDARS ("To-Dos=/path/to-dos.ics,…"; with
    /// "rw:" before the file, one that may be changed).
    explicit SystemCalendars(bool testMode, QObject *parent = nullptr);

    /// A calendar file that is never found: the one in which Kalendar hands
    /// its own events to the system, which would come back as a calendar of
    /// the system otherwise.
    void leaveOut(const QString &file);
    /// Asks anew. The answer arrives as found(), unless Akonadi does not run.
    void refresh();

    /// Kalendar wrote to this calendar file. Whenever another program
    /// changes a calendar's file, Akonadi's agent first keeps a copy of the
    /// calendar as it had it, in its own folder, and nobody ever clears
    /// those away. For the files named here, the old ones are (see
    /// prune()), from now on and at later starts.
    void noteWritten(const QUrl &file);
    /// Where Akonadi's agents keep what is theirs, each in a folder of its
    /// own name. A test names another place.
    void setAgentData(const QString &folder);
    /// Removes Akonadi's copies of the calendar file called @p name from
    /// @p folder ("<name>-1", "<name>-2", …): those that are older than
    /// @p days days, except for the newest @p keep, which stay however old
    /// they are. Nothing else in the folder is touched. Gives how many went.
    static int prune(const QString &folder, const QString &name, int keep = 5, int days = 30);

Q_SIGNALS:
    /// The calendar files there are, each with whether Akonadi lets it be
    /// changed. With @p complete false, an agent did not say where its file
    /// is, so there may be more than these.
    void found(const QList<CalendarInfo> &calendars, bool complete);

private Q_SLOTS:
    /// Akonadi got, lost or renamed an agent, or was started.
    void agentsChanged();

private:
    struct Agent {
        QString instance;
        /// What its settings are called on the bus.
        QString interface;
        /// Whether it has one file, which could be written to.
        bool file = false;
    };
    using Reply = std::function<void(const QDBusMessage &)>;

    void call(const QString &service, const QString &path, const QString &interface, const QString &method, const QVariantList &arguments, const Reply &then,
              int timeout);
    void look();
    /// Asks the agents one after the other, and gives the result when all are asked.
    void ask(quint64 round, QList<Agent> agents, const QList<CalendarInfo> &calendars, bool complete);
    void finish(const QList<CalendarInfo> &calendars, bool complete);
    /// Clears away old copies of the files Kalendar wrote to. With
    /// @p complete, a file that no agent has any more is forgotten.
    void tidy(bool complete);

    const bool m_testMode;
    QString m_leftOut;
    QString m_agentData;
    /// Which agent has which calendar file, as last found.
    QHash<QString, QString> m_agents;
    /// The same for the question that is being asked right now.
    QHash<QString, QString> m_asked;
    /// Akonadi makes its copy a moment after the file changed.
    QTimer *const m_tidy;
    /// Goes up with every question: answers to an earlier one do not count.
    quint64 m_round = 0;
    /// An agent that is new has to be set up before it can be asked.
    QTimer *const m_soon;
    /// For another try after an agent did not answer, a few times.
    QTimer *const m_again;
    int m_tries = 0;
};
