// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kalendareventsplugin.h"

#include "engine.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace
{
QString databasePath()
{
    // Tests point to a database of their own.
    const QString custom = qEnvironmentVariable("KALENDAR_DATABASE");
    if (!custom.isEmpty()) {
        return custom;
    }
    // Kalendar installed as a Flatpak keeps its data in a place of its own.
    // Of the two, the one that was used last is the one in use.
    const QString plain = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/kalendar/calendars.sqlite"_s;
    const QString flatpak = QDir::homePath() + u"/.var/app/io.github.n4n0gh.kalendar/data/kalendar/calendars.sqlite"_s;
    return QFileInfo(flatpak).lastModified() > QFileInfo(plain).lastModified() || !QFileInfo::exists(plain) ? flatpak : plain;
}
}

/// Lives in the plugin's thread, where reading may take its time: opening
/// the database means reading every event there is.
class KalendarEventsPlugin::Reader : public QObject
{
public:
    explicit Reader(const QString &database)
        : m_database(database)
    {
    }

    std::optional<Shown> read(const QDate &start, const QDate &end);

private:
    /// Tells one state of the database from the next.
    struct Stamp {
        QDateTime changed;
        qint64 size = 0;
        QDateTime journalChanged;
        qint64 journalSize = 0;
        bool operator==(const Stamp &) const = default;
    };
    Stamp stamp() const;

    const QString m_database;
    /// The database as it was read last, kept for as long as the files stay
    /// as they are: Plasma asks again whenever the calendar is opened or
    /// another month is shown.
    std::unique_ptr<Engine> m_engine;
    Stamp m_stamp;
};

KalendarEventsPlugin::KalendarEventsPlugin(QObject *parent)
    : CalendarEvents::CalendarEventsPlugin(parent)
    , m_database(databasePath())
    , m_reader(new Reader(m_database))
{
    m_reader->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_reader, &QObject::deleteLater);
    m_thread.start();

    // Kalendar writes in several steps; they are waited out and then read as one.
    m_settle.setSingleShot(true);
    m_settle.setInterval(500);
    connect(&m_settle, &QTimer::timeout, this, &KalendarEventsPlugin::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    watch();
}

KalendarEventsPlugin::~KalendarEventsPlugin()
{
    ++m_serial; // what is still asked for is not wanted any more
    m_thread.quit();
    m_thread.wait();
}

void KalendarEventsPlugin::watch()
{
    // The folder tells when the database appears; the files tell when it
    // changes. Changes go to the file ending in -wal first.
    const QStringList wanted{QFileInfo(m_database).absolutePath(), m_database, m_database + u"-wal"_s};
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    for (const QString &path : wanted) {
        if (!watched.contains(path) && QFileInfo::exists(path)) {
            m_watcher.addPath(path);
        }
    }
}

namespace
{
// Plasma builds its calendar anew for every event that is taken back, one
// after the other, and gets slower each time: a few dozen in a row bring the
// whole shell to a halt. Events are taken back by name, though, so all of
// Kalendar's go by one name and are taken back at once.
const QString Everything = u"io.github.n4n0gh.kalendar"_s;

bool same(const CalendarEvents::EventData &a, const CalendarEvents::EventData &b)
{
    return a.title() == b.title() && a.description() == b.description() && a.location() == b.location() && a.isAllDay() == b.isAllDay()
        && a.startDateTime() == b.startDateTime() && a.endDateTime() == b.endDateTime() && a.eventColor() == b.eventColor();
}
}

void KalendarEventsPlugin::loadEventsForDateRange(const QDate &startDate, const QDate &endDate)
{
    m_start = startDate;
    m_end = endDate;
    // Plasma has dropped what it had before it asks, so there is nothing to
    // take back: what is there now is all it needs.
    m_shown.clear();
    request();
}

void KalendarEventsPlugin::reload()
{
    watch();
    if (m_start.isValid() && m_end.isValid()) {
        request();
    }
}

void KalendarEventsPlugin::request()
{
    const quint64 serial = ++m_serial;
    QMetaObject::invokeMethod(m_reader, [this, reader = m_reader, serial, start = m_start, end = m_end] {
        if (serial != m_serial) {
            return; // asked again meanwhile
        }
        QMetaObject::invokeMethod(this, [this, serial, events = reader->read(start, end)] {
            show(serial, events);
        });
    });
}

void KalendarEventsPlugin::show(quint64 serial, const std::optional<Shown> &events)
{
    // An answer to an earlier question would come on top of the one to the
    // last. If the database is there but cannot be read right now, what is
    // shown stays.
    if (serial != m_serial || !events) {
        return;
    }
    // Not every change of the files is one of the days that are shown; reading
    // them makes files appear next to the database, for one.
    const bool unchanged = events->size() == m_shown.size() && std::all_of(events->keyBegin(), events->keyEnd(), [&](const QString &key) {
        const auto before = m_shown.constFind(key);
        const Entry &entry = *events->constFind(key);
        return before != m_shown.cend() && before->firstDay == entry.firstDay && before->lastDay == entry.lastDay && same(before->event, entry.event);
    });
    if (unchanged) {
        return;
    }
    if (!m_shown.isEmpty()) {
        Q_EMIT eventRemoved(Everything);
    }
    m_shown = *events;
    announce(m_shown);
}

void KalendarEventsPlugin::announce(const Shown &events)
{
    QMultiHash<QDate, CalendarEvents::EventData> data;
    for (const Entry &entry : events) {
        // An event that takes several days is listed on each of them.
        for (QDate day = std::max(entry.firstDay, m_start); day <= std::min(entry.lastDay, m_end); day = day.addDays(1)) {
            data.insert(day, entry.event);
        }
    }
    if (!data.isEmpty()) {
        Q_EMIT dataReady(data);
    }
}

KalendarEventsPlugin::Reader::Stamp KalendarEventsPlugin::Reader::stamp() const
{
    const QFileInfo database(m_database);
    // Changes go to the file ending in -wal first. Reading makes an empty
    // one appear, which is no change.
    const QFileInfo journal(m_database + u"-wal"_s);
    Stamp stamp{database.lastModified(), database.size(), {}, journal.size()};
    if (stamp.journalSize > 0) {
        stamp.journalChanged = journal.lastModified();
    }
    return stamp;
}

std::optional<KalendarEventsPlugin::Shown> KalendarEventsPlugin::Reader::read(const QDate &start, const QDate &end)
{
    Shown result;
    if (!QFileInfo::exists(m_database)) {
        m_engine.reset();
        return result; // Kalendar has not been used yet
    }
    // Looked at before reading, so that a change meanwhile is seen next time.
    const Stamp now = stamp();
    if (!m_engine || now != m_stamp) {
        m_engine = std::make_unique<Engine>();
        m_stamp = now;
        if (!m_engine->openReadOnly(m_database)) {
            m_engine.reset();
            return std::nullopt;
        }
    }
    const Engine &engine = *m_engine;
    const QList<Occurrence> occurrences = engine.occurrences(start, end);
    for (const Occurrence &occurrence : occurrences) {
        const Collection *calendar = engine.collection(occurrence.collectionId);
        Entry entry;
        entry.firstDay = occurrence.firstDay;
        entry.lastDay = occurrence.lastDay;
        CalendarEvents::EventData &event = entry.event;
        event.setUid(Everything);
        event.setEventType(CalendarEvents::EventData::Event);
        event.setTitle(occurrence.event->summary());
        event.setDescription(occurrence.event->description());
        event.setLocation(occurrence.event->location());
        event.setIsAllDay(occurrence.allDay);
        event.setStartDateTime(occurrence.start);
        event.setEndDateTime(occurrence.end);
        if (calendar) {
            event.setEventColor(calendar->color.name());
        }
        result.insert(occurrence.href + u'#' + QString::number(occurrence.start.toMSecsSinceEpoch()), entry);
    }
    return result;
}
