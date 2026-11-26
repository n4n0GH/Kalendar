// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"
#include "importsample.h"

#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace Qt::StringLiterals;

namespace
{
/// A calendar file as another program of this system keeps it: an event, and
/// to-dos, of which @p open are still to be done.
QByteArray systemSample(int open, const QByteArray &name = "To-Dos")
{
    QByteArray ics = "BEGIN:VCALENDAR\r\n"
                     "VERSION:2.0\r\n"
                     "PRODID:-//Elsewhere//Lists//EN\r\n"
                     "CALSCALE:GREGORIAN\r\n"
                     "X-WR-CALNAME:"
        + name
        + "\r\n"
          "BEGIN:VEVENT\r\n"
          "UID:review@elsewhere\r\n"
          "DTSTAMP:20261120T080000Z\r\n"
          "SUMMARY:Review\r\n"
          "DTSTART:20261127T100000\r\n"
          "DTEND:20261127T110000\r\n"
          "END:VEVENT\r\n"
          // Done, and one that is due on no day: neither has a place in a calendar.
          "BEGIN:VTODO\r\n"
          "UID:done@elsewhere\r\n"
          "DTSTAMP:20261120T080000Z\r\n"
          "SUMMARY:Buy stamps\r\n"
          "DUE;VALUE=DATE:20261128\r\n"
          "STATUS:COMPLETED\r\n"
          "COMPLETED:20261121T090000Z\r\n"
          "END:VTODO\r\n"
          "BEGIN:VTODO\r\n"
          "UID:someday@elsewhere\r\n"
          "DTSTAMP:20261120T080000Z\r\n"
          "SUMMARY:Learn the cello\r\n"
          "END:VTODO\r\n";
    if (open >= 1) {
        ics += "BEGIN:VTODO\r\n"
               "UID:tax@elsewhere\r\n"
               "DTSTAMP:20261120T080000Z\r\n"
               "SUMMARY:Send the tax form\r\n"
               "DESCRIPTION:Both pages\r\n"
               "DUE;VALUE=DATE:20261130\r\n"
               "STATUS:NEEDS-ACTION\r\n"
               "END:VTODO\r\n";
    }
    if (open >= 2) {
        ics += "BEGIN:VTODO\r\n"
               "UID:call@elsewhere\r\n"
               "DTSTAMP:20261120T080000Z\r\n"
               "SUMMARY:Call the bank\r\n"
               "DUE:20261130T160000\r\n"
               "END:VTODO\r\n";
    }
    return ics + "END:VCALENDAR\r\n";
}

/// Writes a file the way most programs do: beside it, and then in its place.
void replaceFile(const QString &path, const QByteArray &content)
{
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    QVERIFY(file.commit());
}

/// A calendar as the system names it; one that it lets be changed with @p readOnly false.
CalendarInfo systemCalendar(const QString &path, const QString &name, bool readOnly = true)
{
    CalendarInfo info;
    info.url = QUrl::fromLocalFile(path);
    info.name = name;
    info.readOnly = readOnly;
    return info;
}

QByteArray contentsOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

/// The calendar on this system: no server involved.
class EngineTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

    static QVariantMap eventData(const Engine &engine, const QString &summary, const QDateTime &start, const QDateTime &end, bool allDay = false,
                                 const QString &repeat = u"none"_s)
    {
        return {
            {u"collectionId"_s, engine.collections().first().id},
            {u"summary"_s, summary},
            {u"start"_s, start},
            {u"end"_s, end},
            {u"allDay"_s, allDay},
            {u"repeat"_s, repeat},
        };
    }

private Q_SLOTS:
    void init()
    {
        QFile::remove(m_dir.filePath(u"test.sqlite"_s));
    }

    void startsWithALocalCalendar()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        QCOMPARE(engine.accounts().size(), 1);
        QCOMPARE(engine.accounts().first().type, Account::Local);
        QCOMPARE(engine.collections().size(), 1);
        QVERIFY(engine.collections().first().enabled);
        QVERIFY(engine.occurrences(QDate(2026, 1, 1), QDate(2026, 12, 31)).isEmpty());
    }

    void createsChangesAndKeeps()
    {
        const QDateTime start(QDate(2026, 11, 12), QTime(14, 0));
        {
            Engine engine;
            QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
            QSignalSpy changed(&engine, &Engine::eventsChanged);
            QCOMPARE(engine.saveEvent(eventData(engine, u" Dentist "_s, start, start.addSecs(3600))), QString());
            QVERIFY(changed.size() >= 1);
            QCOMPARE(engine.pendingChanges(), 0);

            const QList<Occurrence> found = engine.occurrences(QDate(2026, 11, 12), QDate(2026, 11, 12));
            QCOMPARE(found.size(), 1);
            QCOMPARE(found.first().event->summary(), u"Dentist"_s);
            QCOMPARE(found.first().start, start);
            QCOMPARE(found.first().end, start.addSecs(3600));
            QVERIFY(engine.occurrences(QDate(2026, 11, 13), QDate(2026, 11, 30)).isEmpty());

            // Change it: an hour later, and a new title.
            QVariantMap data = engine.dayLayout(QDate(2026, 11, 12)).first().toMap();
            data.insert(u"originalCollectionId"_s, data.value(u"collectionId"_s));
            data.insert(u"summary"_s, u"Dentist (moved)"_s);
            data.insert(u"start"_s, start.addSecs(3600));
            data.insert(u"end"_s, start.addSecs(7200));
            QCOMPARE(engine.saveEvent(data), QString());
            QCOMPARE(engine.occurrences(QDate(2026, 11, 12), QDate(2026, 11, 12)).size(), 1);
        }
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        const QList<Occurrence> found = engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30));
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first().event->summary(), u"Dentist (moved)"_s);
        QCOMPARE(found.first().start, start.addSecs(3600));
    }

    void refusesAnEndBeforeTheStart()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        const QDateTime start(QDate(2026, 11, 12), QTime(14, 0));
        QVERIFY(!engine.saveEvent(eventData(engine, u"Backwards"_s, start, start.addSecs(-60))).isEmpty());
        QVERIFY(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30)).isEmpty());
    }

    void wholeDays()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        // Thursday to Saturday.
        const QDateTime first(QDate(2026, 11, 12), QTime(0, 0));
        QCOMPARE(engine.saveEvent(eventData(engine, u"Conference"_s, first, first.addDays(2), true)), QString());
        const QList<Occurrence> found = engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30));
        QCOMPARE(found.size(), 1);
        QVERIFY(found.first().allDay);
        QCOMPARE(found.first().firstDay, QDate(2026, 11, 12));
        QCOMPARE(found.first().lastDay, QDate(2026, 11, 14));
        QVERIFY(engine.occurrences(QDate(2026, 11, 15), QDate(2026, 11, 15)).isEmpty());
        QCOMPARE(engine.occurrences(QDate(2026, 11, 14), QDate(2026, 11, 14)).size(), 1);
        // Not among the events with a time of day.
        QVERIFY(engine.dayLayout(QDate(2026, 11, 13)).isEmpty());
    }

    void repeatsAndSkipsOne()
    {
        Engine engine;
        engine.setUndoPeriod(0);
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        const QDateTime start(QDate(2026, 11, 2), QTime(9, 0)); // a Monday
        QCOMPARE(engine.saveEvent(eventData(engine, u"Stand-up"_s, start, start.addSecs(900), false, u"weekly"_s)), QString());
        QCOMPARE(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30)).size(), 5);
        QCOMPARE(engine.occurrences(QDate(2027, 3, 1), QDate(2027, 3, 1)).size(), 1);

        QVariantMap third = engine.dayLayout(QDate(2026, 11, 16)).first().toMap();
        QCOMPARE(third.value(u"repeat"_s).toString(), u"weekly"_s);
        const QString collectionId = third.value(u"collectionId"_s).toString();
        const QString href = third.value(u"href"_s).toString();

        // Only the third one.
        const int token = engine.deleteEvent(collectionId, href, third.value(u"slot"_s).toDateTime(), false);
        QVERIFY(token != 0);
        QCOMPARE(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30)).size(), 4);
        QVERIFY(engine.dayLayout(QDate(2026, 11, 16)).isEmpty());
        engine.undo(token);
        QCOMPARE(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30)).size(), 5);

        // Moving one appearance by a day and an hour moves them all.
        third = engine.dayLayout(QDate(2026, 11, 16)).first().toMap();
        third.insert(u"originalCollectionId"_s, collectionId);
        third.insert(u"start"_s, QDateTime(QDate(2026, 11, 17), QTime(10, 0)));
        third.insert(u"end"_s, QDateTime(QDate(2026, 11, 17), QTime(10, 15)));
        QCOMPARE(engine.saveEvent(third), QString());
        const QList<Occurrence> moved = engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30));
        QCOMPARE(moved.size(), 4);
        QCOMPARE(moved.first().start, QDateTime(QDate(2026, 11, 3), QTime(10, 0)));

        // All of them, and back again.
        const int all = engine.deleteEvent(collectionId, href, {}, true);
        QVERIFY(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30)).isEmpty());
        engine.undo(all);
        QCOMPARE(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30)).size(), 4);
        QVERIFY(engine.deleteEvent(collectionId, href, {}, true) != 0);
        QTRY_COMPARE(engine.pendingChanges(), 0);
        QVERIFY(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30)).isEmpty());
    }

    void stopsRepeating()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        const QDateTime start(QDate(2026, 11, 2), QTime(9, 0));
        QCOMPARE(engine.saveEvent(eventData(engine, u"Daily"_s, start, start.addSecs(900), false, u"daily"_s)), QString());
        QCOMPARE(engine.occurrences(QDate(2026, 11, 2), QDate(2026, 11, 8)).size(), 7);
        QVariantMap data = engine.dayLayout(QDate(2026, 11, 2)).first().toMap();
        data.insert(u"originalCollectionId"_s, data.value(u"collectionId"_s));
        data.insert(u"repeat"_s, u"none"_s);
        QCOMPARE(engine.saveEvent(data), QString());
        QCOMPARE(engine.occurrences(QDate(2026, 11, 2), QDate(2026, 11, 8)).size(), 1);
    }

    void hiddenCalendarsShowNothing()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        const QDateTime start(QDate(2026, 11, 12), QTime(14, 0));
        QCOMPARE(engine.saveEvent(eventData(engine, u"Dentist"_s, start, start.addSecs(3600))), QString());
        engine.setCollectionEnabled(engine.collections().first().id, false);
        QVERIFY(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30)).isEmpty());
        engine.setCollectionEnabled(engine.collections().first().id, true);
        QCOMPARE(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30)).size(), 1);
    }

    void laysOutAWeek()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        const QDate monday(2026, 11, 9);
        const auto at = [&](int day, int hour) {
            return QDateTime(monday.addDays(day), QTime(hour, 0));
        };
        // A trip from the Saturday before until Wednesday, a holiday on Tuesday, and three things on Tuesday.
        QCOMPARE(engine.saveEvent(eventData(engine, u"Trip"_s, at(-2, 0), at(2, 0), true)), QString());
        QCOMPARE(engine.saveEvent(eventData(engine, u"Holiday"_s, at(1, 0), at(1, 0), true)), QString());
        QCOMPARE(engine.saveEvent(eventData(engine, u"Breakfast"_s, at(1, 8), at(1, 9))), QString());
        QCOMPARE(engine.saveEvent(eventData(engine, u"Lunch"_s, at(1, 12), at(1, 13))), QString());
        QCOMPARE(engine.saveEvent(eventData(engine, u"Dinner"_s, at(1, 19), at(1, 20))), QString());

        QVariantMap layout = engine.weekLayout(monday, 7, false, 0);
        QVariantList segments = layout.value(u"segments"_s).toList();
        QCOMPARE(segments.size(), 5);
        const QVariantMap trip = segments.at(0).toMap();
        QCOMPARE(trip.value(u"title"_s).toString(), u"Trip"_s);
        QCOMPARE(trip.value(u"column"_s).toInt(), 0);
        QCOMPARE(trip.value(u"span"_s).toInt(), 3);
        QCOMPARE(trip.value(u"lane"_s).toInt(), 0);
        QVERIFY(trip.value(u"continuesBefore"_s).toBool());
        QVERIFY(!trip.value(u"continuesAfter"_s).toBool());
        QCOMPARE(segments.at(1).toMap().value(u"lane"_s).toInt(), 1);
        QCOMPARE(segments.at(4).toMap().value(u"title"_s).toString(), u"Dinner"_s);
        QCOMPARE(segments.at(4).toMap().value(u"lane"_s).toInt(), 4);
        QCOMPARE(layout.value(u"lanes"_s).toInt(), 5);

        // With room for three rows, Tuesday shows two events and "3 more".
        layout = engine.weekLayout(monday, 7, false, 3);
        segments = layout.value(u"segments"_s).toList();
        QCOMPARE(segments.size(), 2);
        QCOMPARE(layout.value(u"more"_s).toList().at(1).toInt(), 3);
        QCOMPARE(layout.value(u"more"_s).toList().at(0).toInt(), 0);

        // Only what takes whole days.
        QCOMPARE(engine.weekLayout(monday, 7, true, 0).value(u"segments"_s).toList().size(), 2);
    }

    void laysOutADay()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        const QDate day(2026, 11, 10);
        const auto at = [&](int hour, int minute = 0) {
            return QDateTime(day, QTime(hour, minute));
        };
        QCOMPARE(engine.saveEvent(eventData(engine, u"A"_s, at(9), at(11))), QString());
        QCOMPARE(engine.saveEvent(eventData(engine, u"B"_s, at(10), at(12))), QString());
        QCOMPARE(engine.saveEvent(eventData(engine, u"C"_s, at(11), at(12))), QString());
        QCOMPARE(engine.saveEvent(eventData(engine, u"D"_s, at(15), at(15, 5))), QString());

        const QVariantList blocks = engine.dayLayout(day);
        QCOMPARE(blocks.size(), 4);
        const auto value = [&](int index, const QString &key) {
            return blocks.at(index).toMap().value(key).toInt();
        };
        // A and B overlap; C fits under A, beside B.
        QCOMPARE(value(0, u"column"_s), 0);
        QCOMPARE(value(1, u"column"_s), 1);
        QCOMPARE(value(2, u"column"_s), 0);
        QCOMPARE(value(0, u"columns"_s), 2);
        QCOMPARE(value(2, u"columns"_s), 2);
        QCOMPARE(value(0, u"startMinute"_s), 9 * 60);
        QCOMPARE(value(0, u"endMinute"_s), 11 * 60);
        // D is on its own, and drawn longer than its five minutes.
        QCOMPARE(value(3, u"columns"_s), 1);
        QCOMPARE(value(3, u"endMinute"_s), 15 * 60 + 30);
    }

    void importsACalendarFile()
    {
        // What the file holds, before anything is done with it.
        const CalendarFile file = Engine::inspect(ImportSample);
        QVERIFY(file.valid);
        QCOMPARE(file.events.size(), 2); // the exception belongs to its event
        QCOMPARE(file.others, 1);
        QCOMPARE(file.events.at(0).summary, u"Fair"_s);
        QVERIFY(file.events.at(0).allDay);
        QCOMPARE(file.events.at(0).start, QDateTime(QDate(2026, 11, 5), QTime(0, 0)));
        QCOMPARE(file.events.at(1).summary, u"Call with New York"_s);
        QVERIFY(file.events.at(1).repeats);
        QVERIFY(!Engine::inspect("Dear Ada, see you on Monday.").valid);
        QVERIFY(Engine::inspect("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nEND:VCALENDAR\r\n").events.isEmpty());

        const QTimeZone newYork("America/New_York");
        {
            Engine engine;
            engine.setUndoPeriod(0);
            QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
            const QString calendar = engine.collections().first().id;
            QSignalSpy changed(&engine, &Engine::eventsChanged);

            int imported = -1;
            QVERIFY(!engine.importEvents(u"no such calendar"_s, ImportSample, &imported).isEmpty());
            QVERIFY(!engine.importEvents(calendar, "Dear Ada, see you on Monday.", &imported).isEmpty());
            QCOMPARE(imported, 0);
            QVERIFY(changed.isEmpty());

            QCOMPARE(engine.importEvents(calendar, ImportSample, &imported), QString());
            QCOMPARE(imported, 2);
            QCOMPARE(changed.size(), 1);
            QCOMPARE(engine.pendingChanges(), 0); // nowhere to send them

            // Three calls, the second one two hours late; all at New York's time.
            QList<Occurrence> found = engine.occurrences(QDate(2026, 11, 8), QDate(2026, 12, 31));
            QCOMPARE(found.size(), 3);
            std::sort(found.begin(), found.end(), [](const Occurrence &a, const Occurrence &b) {
                return a.start < b.start;
            });
            QCOMPARE(found.at(0).start, QDateTime(QDate(2026, 11, 16), QTime(10, 0), newYork));
            QCOMPARE(found.at(0).end, QDateTime(QDate(2026, 11, 16), QTime(10, 30), newYork));
            QCOMPARE(found.at(1).start, QDateTime(QDate(2026, 11, 23), QTime(12, 0), newYork));
            QCOMPARE(found.at(1).event->summary(), u"Call with New York (late)"_s);
            QCOMPARE(found.at(2).start, QDateTime(QDate(2026, 11, 30), QTime(10, 0), newYork));
            QCOMPARE(found.at(0).href, found.at(1).href);

            // The fair: the 5th and the 6th, as the end of whole days is the day after.
            found = engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 8));
            QCOMPARE(found.size(), 1);
            QVERIFY(found.first().allDay);
            QCOMPARE(found.first().firstDay, QDate(2026, 11, 5));
            QCOMPARE(found.first().lastDay, QDate(2026, 11, 6));
            QCOMPARE(found.first().event->location(), u"Hall 3"_s);

            // An imported event is one like any other: it can be changed.
            QVariantMap fair = engine.weekLayout(QDate(2026, 11, 5), 1, true, 0).value(u"segments"_s).toList().first().toMap();
            fair.insert(u"originalCollectionId"_s, calendar);
            fair.insert(u"summary"_s, u"Book Fair"_s);
            QCOMPARE(engine.saveEvent(fair), QString());

            // The same file again: nothing comes twice, and the file's version wins.
            QCOMPARE(engine.importEvents(calendar, ImportSample, &imported), QString());
            QCOMPARE(imported, 2);
            QCOMPARE(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 12, 31)).size(), 4);
            QCOMPARE(engine.occurrences(QDate(2026, 11, 5), QDate(2026, 11, 5)).first().event->summary(), u"Fair"_s);

            // An event that was deleted here comes back with the file.
            const Occurrence call = engine.occurrences(QDate(2026, 11, 16), QDate(2026, 11, 16)).first();
            QVERIFY(engine.deleteEvent(calendar, call.href, {}, true) != 0);
            QCOMPARE(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 12, 31)).size(), 1);
            QCOMPARE(engine.importEvents(calendar, ImportSample), QString());
            QCOMPARE(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 12, 31)).size(), 4);
        }
        // All of it is kept.
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        QCOMPARE(engine.occurrences(QDate(2026, 11, 1), QDate(2026, 12, 31)).size(), 4);
    }
    void showsTheCalendarFilesOfTheSystem()
    {
        const QString file = m_dir.filePath(u"to-dos.ics"_s);
        replaceFile(file, systemSample(2));
        const QDate first(2026, 11, 1);
        const QDate last(2026, 11, 30);
        QString calendar;
        {
            Engine engine;
            QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
            QSignalSpy accounts(&engine, &Engine::accountsChanged);
            engine.setSystemCalendars({systemCalendar(file, u"Lists"_s)});
            QCOMPARE(accounts.size(), 1);
            QCOMPARE(engine.accounts().size(), 2);
            QCOMPARE(engine.accounts().last().type, Account::System);
            QVERIFY(!engine.accounts().last().name.isEmpty());
            QCOMPARE(engine.collections().size(), 2);
            const Collection lists = engine.collections().last();
            calendar = lists.id;
            QCOMPARE(lists.name, u"Lists"_s);
            QVERIFY(lists.readOnly);
            QVERIFY(lists.enabled);
            QCOMPARE(engine.accountError(lists.accountId), QString());

            // The event, and the two to-dos that are open and due, when they are due.
            QCOMPARE(engine.occurrences(first, last).size(), 3);
            const QList<Occurrence> review = engine.occurrences(QDate(2026, 11, 27), QDate(2026, 11, 27));
            QCOMPARE(review.size(), 1);
            QCOMPARE(review.first().start, QDateTime(QDate(2026, 11, 27), QTime(10, 0)));
            const QVariantList bars = engine.weekLayout(QDate(2026, 11, 30), 1, true, 0).value(u"segments"_s).toList();
            QCOMPARE(bars.size(), 1);
            const QVariantMap tax = bars.first().toMap();
            QCOMPARE(tax.value(u"title"_s).toString(), u"Send the tax form"_s);
            QCOMPARE(tax.value(u"description"_s).toString(), u"Both pages"_s);
            QCOMPARE(tax.value(u"allDay"_s).toBool(), true);
            QCOMPARE(tax.value(u"todo"_s).toBool(), true);
            QCOMPARE(tax.value(u"readOnly"_s).toBool(), true);
            QCOMPARE(tax.value(u"calendarName"_s).toString(), u"Lists"_s);
            const QVariantList timed = engine.dayLayout(QDate(2026, 11, 30));
            QCOMPARE(timed.size(), 1);
            QCOMPARE(timed.first().toMap().value(u"title"_s).toString(), u"Call the bank"_s);
            QCOMPARE(timed.first().toMap().value(u"start"_s).toDateTime(), QDateTime(QDate(2026, 11, 30), QTime(16, 0)));
            QCOMPARE(timed.first().toMap().value(u"todo"_s).toBool(), true);
            QCOMPARE(engine.dayLayout(QDate(2026, 11, 27)).first().toMap().value(u"todo"_s).toBool(), false);

            // They are another program's: nothing is added, changed or deleted here.
            const QDateTime start(QDate(2026, 11, 12), QTime(14, 0));
            QVariantMap added = eventData(engine, u"Mine"_s, start, start.addSecs(3600));
            added.insert(u"collectionId"_s, calendar);
            QVERIFY(!engine.saveEvent(added).isEmpty());
            QVariantMap changed = tax;
            changed.insert(u"originalCollectionId"_s, calendar);
            changed.insert(u"summary"_s, u"Changed"_s);
            QVERIFY(!engine.saveEvent(changed).isEmpty());
            QCOMPARE(engine.deleteEvent(calendar, tax.value(u"href"_s).toString(), {}, true), 0);
            QVERIFY(!engine.importEvents(calendar, ImportSample).isEmpty());
            engine.removeAccount(lists.accountId);
            QCOMPARE(engine.accounts().size(), 2);
            QCOMPARE(engine.occurrences(first, last).size(), 3);
            QCOMPARE(engine.pendingChanges(), 0);

            // Told the same again, nothing happens; neither when the file is
            // written anew with the same in it, or with other words around the same.
            QSignalSpy events(&engine, &Engine::eventsChanged);
            QSignalSpy collections(&engine, &Engine::collectionsChanged);
            engine.setSystemCalendars({systemCalendar(file, u"Lists"_s)});
            replaceFile(file, systemSample(2));
            engine.readSystemCalendars();
            replaceFile(file, systemSample(2, "Other Words"));
            engine.syncAll();
            QCOMPARE(events.size(), 0);
            QCOMPARE(collections.size(), 0);
            QCOMPARE(accounts.size(), 1);

            // A to-do that is done is gone from the file, and from here by itself.
            replaceFile(file, systemSample(1));
            QTRY_COMPARE(engine.occurrences(first, last).size(), 2);
            QVERIFY(engine.dayLayout(QDate(2026, 11, 30)).isEmpty());
            // Written into, rather than replaced.
            {
                QFile plain(file);
                QVERIFY(plain.open(QIODevice::WriteOnly | QIODevice::Truncate));
                plain.write(systemSample(2));
            }
            QTRY_COMPARE(engine.occurrences(first, last).size(), 3);
            // And once more, to be sure it is still watched after all that.
            replaceFile(file, systemSample(0));
            QTRY_COMPARE(engine.occurrences(first, last).size(), 1);

            // Another name for the calendar; hidden, it stays hidden.
            engine.setCollectionEnabled(calendar, false);
            QVERIFY(engine.occurrences(first, last).isEmpty());
            engine.setSystemCalendars({systemCalendar(file, u"Renamed"_s)});
            QCOMPARE(engine.collections().size(), 2);
            QCOMPARE(engine.collection(calendar)->name, u"Renamed"_s);
            QVERIFY(!engine.collection(calendar)->enabled);
            engine.setCollectionEnabled(calendar, true);
        }
        // Changed while Kalendar did not run.
        replaceFile(file, systemSample(1));
        {
            // All of it is kept, and what changed is read at the start.
            Engine engine;
            QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
            QCOMPARE(engine.accounts().size(), 2);
            QVERIFY(!engine.accounts().last().name.isEmpty());
            QCOMPARE(engine.collection(calendar)->name, u"Renamed"_s);
            QCOMPARE(engine.occurrences(first, last).size(), 2);
            replaceFile(file, systemSample(2));
            QTRY_COMPARE(engine.occurrences(first, last).size(), 3);

            // What is not a calendar file is said, and what was read before stays.
            QSignalSpy errors(&engine, &Engine::errorsChanged);
            replaceFile(file, "No calendar at all\n");
            QTRY_COMPARE(errors.size(), 1);
            QVERIFY(engine.accountError(u"system"_s).contains("Renamed"_L1));
            QCOMPARE(engine.occurrences(first, last).size(), 3);
            replaceFile(file, systemSample(1));
            QTRY_COMPARE(errors.size(), 2);
            QCOMPARE(engine.accountError(u"system"_s), QString());
            QCOMPARE(engine.occurrences(first, last).size(), 2);

            // A second calendar, whose file is not there yet.
            const QString later = m_dir.filePath(u"later.ics"_s);
            QFile::remove(later);
            engine.setSystemCalendars({systemCalendar(file, u"Renamed"_s), systemCalendar(later, u"Later"_s), systemCalendar(later, u"Twice"_s)});
            QCOMPARE(engine.collections().size(), 3);
            QCOMPARE(engine.accountError(u"system"_s), QString());
            QCOMPARE(engine.occurrences(first, last).size(), 2);
            replaceFile(later, systemSample(0));
            QTRY_COMPARE(engine.occurrences(first, last).size(), 3);

            // Where not every calendar could be asked about, none is taken to be gone.
            engine.setSystemCalendars({systemCalendar(later, u"Later"_s)}, false);
            QCOMPARE(engine.collections().size(), 3);
            engine.setSystemCalendars({}, false);
            QCOMPARE(engine.collections().size(), 3);
            // One that is gone takes its events along; the last one, the account.
            engine.setSystemCalendars({systemCalendar(later, u"Later"_s)});
            QCOMPARE(engine.collections().size(), 2);
            QCOMPARE(engine.occurrences(first, last).size(), 1);
            engine.setSystemCalendars({});
            QCOMPARE(engine.accounts().size(), 1);
            QCOMPARE(engine.collections().size(), 1);
            QVERIFY(engine.occurrences(first, last).isEmpty());
        }
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        QCOMPARE(engine.accounts().size(), 1);
        QVERIFY(engine.occurrences(first, last).isEmpty());
    }

    void readsAFolderOfCalendarFiles()
    {
        // Akonadi also keeps calendars as a folder with a file for each entry.
        const QString folder = m_dir.filePath(u"entries"_s);
        QVERIFY(QDir().mkpath(folder));
        replaceFile(folder + u"/one"_s, systemSample(0));
        replaceFile(folder + u"/WARNING_README.txt"_s, "Do not create or copy files here.\n");
        replaceFile(folder + u"/stray.txt"_s, "Not an entry\n");
        const QDate first(2026, 11, 1);
        const QDate last(2026, 11, 30);

        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        engine.setSystemCalendars({systemCalendar(folder, u"Entries"_s)});
        QCOMPARE(engine.accountError(u"system"_s), QString());
        QCOMPARE(engine.occurrences(first, last).size(), 1);

        QByteArray second = systemSample(1);
        second.replace("review@elsewhere", "second@elsewhere");
        replaceFile(folder + u"/two"_s, second);
        QTRY_COMPARE(engine.occurrences(first, last).size(), 3);
        QVERIFY(QFile::remove(folder + u"/one"_s));
        QTRY_COMPARE(engine.occurrences(first, last).size(), 2);
    }
    void keepsACalendarFileForOtherPrograms()
    {
        const QString file = m_dir.filePath(u"out/kalendar.ics"_s);
        QFile::remove(file);
        const auto read = [&] {
            QFile opened(file);
            return opened.open(QIODevice::ReadOnly) ? opened.readAll() : QByteArray();
        };
        Engine engine;
        engine.setUndoPeriod(0);
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        const QString calendar = engine.collections().first().id;
        // An event that repeats in another time zone with one exception, and one of whole days.
        QCOMPARE(engine.importEvents(calendar, ImportSample), QString());
        const QDateTime start(QDate(2026, 11, 12), QTime(14, 0));
        QCOMPARE(engine.saveEvent(eventData(engine, u"Dentist"_s, start, start.addSecs(3600))), QString());
        // A calendar of the system is another program's already, and stays out.
        const QString other = m_dir.filePath(u"other.ics"_s);
        replaceFile(other, systemSample(2));
        engine.setSystemCalendars({systemCalendar(other, u"Lists"_s)});
        const int shown = engine.occurrences(QDate(2026, 11, 1), QDate(2026, 12, 31)).size();

        // No file until one is wanted.
        QVERIFY(!QFile::exists(file));
        QVERIFY(engine.setExportFile(file));
        const QByteArray first = read();
        QCOMPARE(first, engine.exportedCalendar());
        QVERIFY(first.startsWith("BEGIN:VCALENDAR\r\n"));
        QVERIFY(first.endsWith("END:VCALENDAR\r\n"));
        QVERIFY(first.contains("SUMMARY:Dentist"));
        QVERIFY(!first.contains("Send the tax form"));
        QVERIFY(!first.contains("SUMMARY:Review"));
        QVERIFY(!first.contains("VTODO"));
        // The events as they are written, the exception and the time zone
        // among them, and the time zone once.
        QCOMPARE(first.count("BEGIN:VEVENT"), 4);
        QCOMPARE(first.count("RECURRENCE-ID"), 1);
        QCOMPARE(first.count("BEGIN:VTIMEZONE"), first.count("END:VTIMEZONE"));
        QCOMPARE(first.count("TZID:America/New_York"), 1);
        // It reads as the calendar it is: the same events on the same days.
        const CalendarFile inspected = Engine::inspect(first);
        QVERIFY(inspected.valid);
        QCOMPARE(inspected.events.size(), 3);
        QCOMPARE(inspected.others, 0);
        {
            Engine reader;
            QVERIFY(reader.open(m_dir.filePath(u"reader.sqlite"_s)));
            QCOMPARE(reader.importEvents(reader.collections().first().id, first), QString());
            QCOMPARE(reader.occurrences(QDate(2026, 11, 1), QDate(2026, 12, 31)).size(), shown - 3);
        }

        // It follows the calendar by itself, a moment later.
        QCOMPARE(engine.saveEvent(eventData(engine, u"Cinema"_s, start.addDays(1), start.addDays(1).addSecs(7200))), QString());
        QCOMPARE(read(), first);
        QTRY_VERIFY_WITH_TIMEOUT(read().contains("SUMMARY:Cinema"), 5000);
        const Occurrence dentist = engine.occurrences(QDate(2026, 11, 12), QDate(2026, 11, 12)).first();
        QVERIFY(engine.deleteEvent(calendar, dentist.href, {}, true) != 0);
        QTRY_VERIFY_WITH_TIMEOUT(!read().contains("SUMMARY:Dentist"), 5000);
        QVERIFY(read().contains("SUMMARY:Cinema"));

        // What changes nothing leaves the file as it is, for those who watch it.
        const QDateTime written = QFileInfo(file).lastModified();
        QTest::qWait(1100);
        QVERIFY(engine.setExportFile(file));
        engine.syncAll();
        QTest::qWait(1500);
        QCOMPARE(QFileInfo(file).lastModified(), written);

        // A calendar that is hidden is not handed on.
        engine.setCollectionEnabled(calendar, false);
        QTRY_COMPARE_WITH_TIMEOUT(read().count("BEGIN:VEVENT"), 0, 5000);
        QVERIFY(Engine::inspect(read()).valid);
        engine.setCollectionEnabled(calendar, true);
        QTRY_VERIFY_WITH_TIMEOUT(read().contains("SUMMARY:Cinema"), 5000);

        // No longer wanted: the file is left to whoever asked for it, and not written again.
        QVERIFY(!engine.setExportFile({}));
        const QByteArray last = read();
        QCOMPARE(engine.saveEvent(eventData(engine, u"Yoga"_s, start.addDays(2), start.addDays(2).addSecs(3600))), QString());
        QTest::qWait(1500);
        QCOMPARE(read(), last);
    }
    void changesACalendarFileOfTheSystem()
    {
        // A calendar the system lets be changed: next to what Kalendar
        // shows of it, the file holds things Kalendar knows nothing of.
        const QString file = m_dir.filePath(u"shared.ics"_s);
        const QByteArray longUid = QByteArray(90, 'x') + "@elsewhere";
        QByteArray sample = systemSample(2, "Shared");
        // A long line, which goes on in the next.
        const QByteArray longLine = "UID:" + longUid.first(71) + "\r\n " + longUid.sliced(71) + "\r\n";
        const QByteArray longEvent = "BEGIN:VEVENT\r\n" + longLine
            + "DTSTAMP:20261120T080000Z\r\n"
              "SUMMARY:Long\r\n"
              "DTSTART;VALUE=DATE:20261125\r\n"
              "X-ELSEWHERE-ONLY:kept\r\n"
              "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Long\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n"
              "END:VEVENT\r\n"
              "END:VCALENDAR\r\n";
        sample.replace("END:VCALENDAR\r\n", longEvent);
        replaceFile(file, sample);
        const QDate first(2026, 11, 1);
        const QDate last(2026, 11, 30);
        const QDateTime start(QDate(2026, 11, 12), QTime(14, 0));
        QString calendar;
        {
            Engine engine;
            QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
            engine.setSystemCalendars({systemCalendar(file, u"Shared"_s, false)});
            calendar = engine.collections().last().id;
            QVERIFY(!engine.collection(calendar)->readOnly);
            QCOMPARE(engine.occurrences(first, last).size(), 4); // two events and two to-dos
            const auto save = [&](const QString &summary, int day) {
                QVariantMap data = eventData(engine, summary, start.addDays(day), start.addDays(day).addSecs(3600));
                data.insert(u"collectionId"_s, calendar);
                return engine.saveEvent(data);
            };

            // A new event goes into the file, and everything else in it stays.
            QSignalSpy written(&engine, &Engine::systemCalendarWritten);
            QCOMPARE(save(u"Mine"_s, 0), QString());
            QCOMPARE(written.size(), 1);
            QCOMPARE(written.first().first().toUrl(), QUrl::fromLocalFile(file));
            QCOMPARE(engine.pendingChanges(), 0);
            QCOMPARE(engine.accountError(u"system"_s), QString());
            QByteArray now = contentsOf(file);
            QVERIFY(now.contains("SUMMARY:Mine\r\n"));
            QVERIFY(now.contains("SUMMARY:Review\r\n"));
            QVERIFY(now.contains("X-WR-CALNAME:Shared\r\n"));
            QVERIFY(now.contains("X-ELSEWHERE-ONLY:kept\r\n"));
            QCOMPARE(now.count("BEGIN:VTODO"), 4);
            QCOMPARE(now.count("BEGIN:VEVENT"), 3);
            QCOMPARE(now.count("BEGIN:VCALENDAR"), 1);
            QVERIFY(now.endsWith("END:VCALENDAR\r\n"));
            QVERIFY(Engine::inspect(now).valid);
            QCOMPARE(engine.occurrences(first, last).size(), 5);

            // An event of the file is changed: the one in the file is the changed one.
            QVariantMap review = engine.dayLayout(QDate(2026, 11, 27)).first().toMap();
            QCOMPARE(review.value(u"readOnly"_s).toBool(), false);
            review.insert(u"originalCollectionId"_s, calendar);
            review.insert(u"summary"_s, u"Reviewed"_s);
            QCOMPARE(engine.saveEvent(review), QString());
            now = contentsOf(file);
            QCOMPARE(now.count("UID:review@elsewhere"), 1);
            QVERIFY(now.contains("SUMMARY:Reviewed\r\n"));
            QVERIFY(!now.contains("SUMMARY:Review\r\n"));
            // Also one whose name is written over two lines.
            QVariantMap longOne = engine.weekLayout(QDate(2026, 11, 25), 1, true, 0).value(u"segments"_s).toList().first().toMap();
            QCOMPARE(longOne.value(u"title"_s).toString(), u"Long"_s);
            longOne.insert(u"originalCollectionId"_s, calendar);
            longOne.insert(u"summary"_s, u"Long, changed"_s);
            QCOMPARE(engine.saveEvent(longOne), QString());
            now = contentsOf(file);
            QVERIFY(!now.contains("SUMMARY:Long\r\n"));
            QCOMPARE(now.count("SUMMARY:Long"), 1);
            QCOMPARE(now.count("BEGIN:VEVENT"), 3);
            QCOMPARE(now.count("BEGIN:VTODO"), 4);
            QCOMPARE(engine.occurrences(first, last).size(), 5);
            QCOMPARE(engine.pendingChanges(), 0);

            // A to-do is only shown, in a calendar that can be changed too.
            QVariantMap tax = engine.weekLayout(QDate(2026, 11, 30), 1, true, 0).value(u"segments"_s).toList().first().toMap();
            QCOMPARE(tax.value(u"todo"_s).toBool(), true);
            QCOMPARE(tax.value(u"readOnly"_s).toBool(), true);
            tax.insert(u"originalCollectionId"_s, calendar);
            tax.insert(u"summary"_s, u"Changed"_s);
            QVERIFY(!engine.saveEvent(tax).isEmpty());
            QCOMPARE(engine.deleteEvent(calendar, tax.value(u"href"_s).toString(), {}, true), 0);
            QCOMPARE(contentsOf(file), now);

            // Another program wrote to the file a moment ago, which is not
            // known here yet: both changes are in the file afterwards.
            QByteArray elsewhere = now;
            elsewhere.replace("END:VCALENDAR\r\n",
                              "BEGIN:VEVENT\r\nUID:meanwhile@elsewhere\r\nDTSTAMP:20261120T080000Z\r\nSUMMARY:Meanwhile\r\n"
                              "DTSTART;VALUE=DATE:20261126\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
            replaceFile(file, elsewhere);
            QCOMPARE(save(u"Also mine"_s, 1), QString());
            now = contentsOf(file);
            QVERIFY(now.contains("SUMMARY:Meanwhile\r\n"));
            QVERIFY(now.contains("SUMMARY:Also mine\r\n"));
            QCOMPARE(now.count("BEGIN:VEVENT"), 5);
            QCOMPARE(engine.occurrences(first, last).size(), 7);

            // Deleted, and taken back: the file never heard of it.
            engine.setUndoPeriod(400);
            const Occurrence mine = engine.occurrences(QDate(2026, 11, 12), QDate(2026, 11, 12)).first();
            int token = engine.deleteEvent(calendar, mine.href, {}, true);
            QVERIFY(token != 0);
            QCOMPARE(engine.occurrences(first, last).size(), 6);
            QVERIFY(contentsOf(file).contains("SUMMARY:Mine\r\n"));
            engine.undo(token);
            QTest::qWait(700);
            QVERIFY(contentsOf(file).contains("SUMMARY:Mine\r\n"));
            QCOMPARE(engine.occurrences(first, last).size(), 7);
            QCOMPARE(engine.pendingChanges(), 0);
            // Deleted for good.
            token = engine.deleteEvent(calendar, mine.href, {}, true);
            QVERIFY(token != 0);
            QTRY_VERIFY(!contentsOf(file).contains("SUMMARY:Mine\r\n"));
            QCOMPARE(contentsOf(file).count("BEGIN:VEVENT"), 4);
            QCOMPARE(contentsOf(file).count("BEGIN:VTODO"), 4);
            QCOMPARE(engine.pendingChanges(), 0);
            QCOMPARE(engine.occurrences(first, last).size(), 6);

            // The folder cannot be written to right now: the change waits,
            // it is said why, and it is written when that is over.
            const QString folder = m_dir.filePath(u"locked"_s);
            QVERIFY(QDir().mkpath(folder));
            const QString locked = folder + u"/locked.ics"_s;
            replaceFile(locked, systemSample(0));
            engine.setSystemCalendars({systemCalendar(file, u"Shared"_s, false), systemCalendar(locked, u"Locked"_s, false)});
            const QString lockedCalendar = engine.collections().last().id;
            QVERIFY(!engine.collection(lockedCalendar)->readOnly);
            QFile::setPermissions(folder, QFile::ReadOwner | QFile::ExeOwner);
            if (!QFileInfo(folder).isWritable()) {
                QVariantMap data = eventData(engine, u"Waiting"_s, start, start.addSecs(3600));
                data.insert(u"collectionId"_s, lockedCalendar);
                QCOMPARE(engine.saveEvent(data), QString());
                QCOMPARE(engine.pendingChanges(), 1);
                QVERIFY(engine.accountError(u"system"_s).contains("Locked"_L1));
                QVERIFY(!contentsOf(locked).contains("Waiting"));
                QCOMPARE(engine.occurrences(QDate(2026, 11, 12), QDate(2026, 11, 12)).size(), 1);
                // Looked at again meanwhile, it is still there.
                engine.syncAll();
                QCOMPARE(engine.pendingChanges(), 1);
                QCOMPARE(engine.occurrences(QDate(2026, 11, 12), QDate(2026, 11, 12)).size(), 1);
                QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
                engine.syncAll();
                QCOMPARE(engine.pendingChanges(), 0);
                QCOMPARE(engine.accountError(u"system"_s), QString());
                QVERIFY(contentsOf(locked).contains("SUMMARY:Waiting\r\n"));
            }
            QFile::setPermissions(folder, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

            // A file that cannot be written makes a calendar that cannot be
            // changed, whatever the system says; so does a folder of files.
            QFile::setPermissions(locked, QFile::ReadOwner);
            const QString entries = m_dir.filePath(u"entries-rw"_s);
            QVERIFY(QDir().mkpath(entries));
            // And one whose file is not there yet can be: the file is made.
            const QString fresh = m_dir.filePath(u"fresh.ics"_s);
            QFile::remove(fresh);
            engine.setSystemCalendars({systemCalendar(file, u"Shared"_s, false),
                                       systemCalendar(locked, u"Locked"_s, false),
                                       systemCalendar(entries, u"Entries"_s, false),
                                       systemCalendar(fresh, u"Fresh"_s, false)});
            QCOMPARE(engine.collections().size(), 5);
            QVERIFY(!engine.collection(calendar)->readOnly);
            if (!QFileInfo(locked).isWritable()) {
                QVERIFY(engine.collection(lockedCalendar)->readOnly);
            }
            QVERIFY(engine.collections().at(3).readOnly);
            const QString freshCalendar = engine.collections().at(4).id;
            QVERIFY(!engine.collection(freshCalendar)->readOnly);
            QVariantMap data = eventData(engine, u"First"_s, start, start.addSecs(3600));
            data.insert(u"collectionId"_s, freshCalendar);
            QCOMPARE(engine.saveEvent(data), QString());
            QVERIFY(Engine::inspect(contentsOf(fresh)).valid);
            QCOMPARE(Engine::inspect(contentsOf(fresh)).events.size(), 1);
            QCOMPARE(engine.pendingChanges(), 0);
            // Where not all could be asked about, what could be changed still can.
            engine.setSystemCalendars({}, false);
            QVERIFY(!engine.collection(calendar)->readOnly);
            // The system no longer lets it be changed.
            engine.setSystemCalendars({systemCalendar(file, u"Shared"_s, true)});
            QVERIFY(engine.collection(calendar)->readOnly);
            QVERIFY(!save(u"Too late"_s, 3).isEmpty());
        }
        // All of it is there at the next start, as the file has it.
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"test.sqlite"_s)));
        QCOMPARE(engine.pendingChanges(), 0);
        QCOMPARE(engine.occurrences(first, last).size(), 6);
        QCOMPARE(Engine::inspect(contentsOf(file)).events.size(), 4);
    }
};

QTEST_GUILESS_MAIN(EngineTest)

#include "enginetest.moc"
