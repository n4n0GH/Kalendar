// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"
#include "importsample.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace Qt::StringLiterals;

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
};

QTEST_GUILESS_MAIN(EngineTest)

#include "enginetest.moc"
