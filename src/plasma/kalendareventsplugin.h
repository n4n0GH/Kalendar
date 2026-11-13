// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <CalendarEvents/CalendarEventsPlugin>

#include <QDate>
#include <QFileSystemWatcher>
#include <QHash>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <optional>

/// Shows Kalendar's events in the calendar of Plasma's clock.
///
/// It runs inside the Plasma shell, so it only ever reads, and never makes
/// the shell wait: the events come straight from Kalendar's database, which
/// a thread of its own reads and keeps in memory until Kalendar changes it.
/// If the days that are shown differ then, their events are taken back in
/// one go and handed over anew. Calendars that are hidden in Kalendar are
/// hidden here too.
class KalendarEventsPlugin : public CalendarEvents::CalendarEventsPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.CalendarEventsPlugin" FILE "kalendarevents.json")
    Q_INTERFACES(CalendarEvents::CalendarEventsPlugin)

public:
    explicit KalendarEventsPlugin(QObject *parent = nullptr);
    ~KalendarEventsPlugin() override;

    /// Returns at once; the events follow when they have been read.
    void loadEventsForDateRange(const QDate &startDate, const QDate &endDate) override;

private:
    /// An event as it was handed over, and the days it is listed on.
    struct Entry {
        CalendarEvents::EventData event;
        QDate firstDay;
        QDate lastDay;
    };
    using Shown = QHash<QString, Entry>;

    /// Reads the database, in a thread of its own.
    class Reader;

    void watch();
    void reload();
    /// Asks the reader for the events of the days Plasma asked for.
    void request();
    /// What the reader found for request number @p serial; nothing if the
    /// database is there but cannot be read.
    void show(quint64 serial, const std::optional<Shown> &events);
    void announce(const Shown &events);

    const QString m_database;
    QThread m_thread;
    Reader *const m_reader;
    /// The number of the last request. Plasma often asks several times in a
    /// row; only the last one is worth an answer.
    std::atomic<quint64> m_serial = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QDate m_start;
    QDate m_end;
    /// The events that were handed over, each by a name of its own, to tell
    /// whether anything differs when they are read again.
    Shown m_shown;
};
