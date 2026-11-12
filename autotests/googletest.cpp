// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"
#include "fakegoogleserver.h"
#include "googleclient.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>

#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace Qt::StringLiterals;
using KCalendarCore::Event;

/// Google's Calendar API: how its events become calendar files and back,
/// and synchronizing with a stand-in for it.
class GoogleTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    FakeGoogleServer m_google;
    const QString m_calendar = u"calendar/v3/calendars/ada%40example.org/events"_s;

    static Event::List parse(const QByteArray &ics)
    {
        const auto calendar = KCalendarCore::MemoryCalendar::Ptr::create(QTimeZone::systemTimeZone());
        return KCalendarCore::ICalFormat().fromRawString(calendar, ics) ? calendar->rawEvents() : Event::List();
    }

    static QJsonObject timed(const QString &id, const QString &summary, const QString &start, const QString &end)
    {
        return {{"id"_L1, id},
                {"iCalUID"_L1, QString(id + u"@google.com"_s)},
                {"etag"_L1, u"\"1\""_s},
                {"summary"_L1, summary},
                {"start"_L1, QJsonObject{{"dateTime"_L1, start}, {"timeZone"_L1, u"Europe/Berlin"_s}}},
                {"end"_L1, QJsonObject{{"dateTime"_L1, end}, {"timeZone"_L1, u"Europe/Berlin"_s}}}};
    }

    void addAccount(Engine &engine)
    {
        engine.setAuthProvider([](const Account &, const Engine::AuthCallback &done) {
            done("Bearer test-token-1", {});
        });
        Discovery found;
        bool done = false;
        engine.discoverGoogle(m_google.api(), "Bearer test-token-1", [&](const Discovery &discovery) {
            found = discovery;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(found.error, QString());
        Account account;
        account.type = Account::Google;
        account.name = u"Ada"_s;
        account.user = u"1"_s;
        account.url = found.home;
        engine.addAccount(account, found.calendars);
        QTRY_VERIFY(!engine.isSyncing());
    }

    static void sync(Engine &engine)
    {
        engine.syncAll();
        QTRY_VERIFY_WITH_TIMEOUT(!engine.isSyncing(), 10000);
        QCOMPARE(engine.accountError(engine.accounts().last().id), QString());
    }

    static QString calendarId(const Engine &engine, const QString &name)
    {
        for (const Collection &collection : engine.collections()) {
            if (collection.name == name) {
                return collection.id;
            }
        }
        return {};
    }

    static QStringList titles(const Engine &engine)
    {
        QStringList result;
        const QList<Occurrence> found = engine.occurrences(QDate(2026, 11, 1), QDate(2026, 11, 30));
        for (const Occurrence &occurrence : found) {
            result.append(occurrence.event->summary());
        }
        result.sort();
        return result;
    }

    static QVariantMap find(const Engine &engine, const QString &title, const QDate &day = {})
    {
        const QVariantList segments = engine.weekLayout(QDate(2026, 11, 1), 30, false, 0).value(u"segments"_s).toList();
        for (const QVariant &segment : segments) {
            QVariantMap data = segment.toMap();
            if (data.value(u"title"_s).toString() == title && (!day.isValid() || data.value(u"start"_s).toDateTime().date() == day)) {
                data.insert(u"originalCollectionId"_s, data.value(u"collectionId"_s));
                return data;
            }
        }
        return {};
    }

    QJsonObject onServer(const QString &title) const
    {
        // The stand-in lists two at a time.
        for (int first = 0; first < 100; first += 2) {
            const QJsonArray items = m_google.request("GET", m_calendar + u"?pageToken=%1"_s.arg(first)).value("items"_L1).toArray();
            if (items.isEmpty()) {
                break;
            }
            for (const QJsonValue &item : items) {
                if (item.toObject().value("summary"_L1).toString() == title && !item.toObject().contains("recurringEventId"_L1)) {
                    return item.toObject();
                }
            }
        }
        return {};
    }

private Q_SLOTS:
    void readsEvents()
    {
        const QUrl collection(u"https://example.org/calendars/a/events"_s);
        QJsonObject allDay{{"id"_L1, u"trip"_s},
                           {"etag"_L1, u"\"2\""_s},
                           {"summary"_L1, u"Trip"_s},
                           {"location"_L1, u"Lisbon"_s},
                           {"start"_L1, QJsonObject{{"date"_L1, u"2026-11-20"_s}}},
                           {"end"_L1, QJsonObject{{"date"_L1, u"2026-11-23"_s}}}};
        const QJsonObject gone{{"id"_L1, u"gone"_s}, {"status"_L1, u"cancelled"_s}};
        const QList<FetchedItem> items =
            GoogleClient::toItems(collection, {timed(u"review"_s, u"Review"_s, u"2026-11-12T14:00:00+01:00"_s, u"2026-11-12T14:30:00+01:00"_s), allDay, gone});
        QCOMPARE(items.size(), 2);

        QCOMPARE(items.at(0).href, u"https://example.org/calendars/a/events/review"_s);
        QCOMPARE(items.at(0).etag, u"\"1\""_s);
        const Event::Ptr review = parse(items.at(0).ics).value(0);
        QVERIFY(review);
        QCOMPARE(review->uid(), u"review@google.com"_s);
        QCOMPARE(review->summary(), u"Review"_s);
        QVERIFY(!review->allDay());
        QCOMPARE(review->dtStart(), QDateTime(QDate(2026, 11, 12), QTime(14, 0), QTimeZone("Europe/Berlin")));
        QCOMPARE(review->dtStart().timeZone().id(), QByteArray("Europe/Berlin"));
        QCOMPARE(review->dtEnd(), QDateTime(QDate(2026, 11, 12), QTime(14, 30), QTimeZone("Europe/Berlin")));

        // Google names the day after the last one; a calendar file's reader the last one.
        const Event::Ptr trip = parse(items.at(1).ics).value(0);
        QVERIFY(trip);
        QVERIFY(trip->allDay());
        QCOMPARE(trip->location(), u"Lisbon"_s);
        QCOMPARE(trip->dtStart().date(), QDate(2026, 11, 20));
        QCOMPARE(trip->dtEnd().date(), QDate(2026, 11, 22));
    }

    void readsRepeatingEventsWithTheirExceptions()
    {
        const QUrl collection(u"https://example.org/calendars/a/events"_s);
        QJsonObject standup = timed(u"standup"_s, u"Stand-up"_s, u"2026-11-02T09:00:00+01:00"_s, u"2026-11-02T09:15:00+01:00"_s);
        standup.insert("recurrence"_L1, QJsonArray{u"RRULE:FREQ=WEEKLY;BYDAY=MO"_s});
        QJsonObject moved = timed(u"standup_20261109T080000Z"_s, u"Stand-up (late)"_s, u"2026-11-09T11:00:00+01:00"_s, u"2026-11-09T11:15:00+01:00"_s);
        moved.insert("recurringEventId"_L1, u"standup"_s);
        moved.insert("originalStartTime"_L1, QJsonObject{{"dateTime"_L1, u"2026-11-09T09:00:00+01:00"_s}, {"timeZone"_L1, u"Europe/Berlin"_s}});
        const QJsonObject skipped{{"id"_L1, u"standup_20261116T080000Z"_s},
                                  {"etag"_L1, u"\"7\""_s},
                                  {"status"_L1, u"cancelled"_s},
                                  {"recurringEventId"_L1, u"standup"_s},
                                  {"originalStartTime"_L1, QJsonObject{{"dateTime"_L1, u"2026-11-16T09:00:00+01:00"_s}}}};

        const QList<FetchedItem> plain = GoogleClient::toItems(collection, {standup});
        const QList<FetchedItem> items = GoogleClient::toItems(collection, {moved, standup, skipped});
        // One file for the event and what belongs to it.
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().href, u"https://example.org/calendars/a/events/standup"_s);
        // A change to an appearance is a change to the file.
        QVERIFY(items.first().etag != plain.first().etag);
        QCOMPARE(items.first().etag.section(u'|', 0, 0), plain.first().etag);

        Event::Ptr master;
        Event::Ptr exception;
        for (const Event::Ptr &event : parse(items.first().ics)) {
            (event->hasRecurrenceId() ? exception : master) = event;
        }
        QVERIFY(master && exception);
        QVERIFY(master->recurs());
        const QTimeZone berlin("Europe/Berlin");
        QVERIFY(master->recursOn(QDate(2026, 11, 23), berlin));
        QVERIFY(!master->recursOn(QDate(2026, 11, 16), berlin));
        QCOMPARE(exception->uid(), master->uid());
        QCOMPARE(exception->summary(), u"Stand-up (late)"_s);
        QCOMPARE(exception->recurrenceId(), QDateTime(QDate(2026, 11, 9), QTime(9, 0), berlin));
        QCOMPARE(exception->dtStart(), QDateTime(QDate(2026, 11, 9), QTime(11, 0), berlin));
    }

    void writesEvents()
    {
        const Event::Ptr event = Event::Ptr::create();
        event->setSummary(u"Stand-up"_s);
        event->setDtStart(QDateTime(QDate(2026, 11, 2), QTime(9, 0), QTimeZone("Europe/Berlin")));
        event->setDtEnd(QDateTime(QDate(2026, 11, 2), QTime(9, 15), QTimeZone("Europe/Berlin")));
        event->recurrence()->setWeekly(1);
        event->recurrence()->addExDateTime(QDateTime(QDate(2026, 11, 16), QTime(9, 0), QTimeZone("Europe/Berlin")));
        QJsonObject json = GoogleClient::toJson(event);
        QCOMPARE(json.value("summary"_L1).toString(), u"Stand-up"_s);
        QCOMPARE(json.value("start"_L1).toObject().value("dateTime"_L1).toString(), u"2026-11-02T09:00:00+01:00"_s);
        QCOMPARE(json.value("start"_L1).toObject().value("timeZone"_L1).toString(), u"Europe/Berlin"_s);
        QCOMPARE(json.value("end"_L1).toObject().value("dateTime"_L1).toString(), u"2026-11-02T09:15:00+01:00"_s);
        const QJsonArray rules = json.value("recurrence"_L1).toArray();
        QCOMPARE(rules.size(), 2);
        QVERIFY2(rules.at(0).toString().startsWith("RRULE:FREQ=WEEKLY"_L1), qPrintable(rules.at(0).toString()));
        QVERIFY2(rules.at(1).toString().startsWith("EXDATE"_L1) && rules.at(1).toString().contains("20261116T"_L1), qPrintable(rules.at(1).toString()));

        // And back again.
        json.insert("id"_L1, u"standup"_s);
        const Event::Ptr back = parse(GoogleClient::toItems(QUrl(u"https://example.org/e"_s), {json}).value(0).ics).value(0);
        QVERIFY(back);
        QCOMPARE(back->dtStart(), event->dtStart());
        QVERIFY(back->recursOn(QDate(2026, 11, 9), QTimeZone("Europe/Berlin")));
        QVERIFY(!back->recursOn(QDate(2026, 11, 16), QTimeZone("Europe/Berlin")));

        const Event::Ptr trip = Event::Ptr::create();
        trip->setAllDay(true);
        trip->setDtStart(QDateTime(QDate(2026, 11, 20), QTime(0, 0)));
        trip->setDtEnd(QDateTime(QDate(2026, 11, 22), QTime(0, 0)));
        json = GoogleClient::toJson(trip);
        QCOMPARE(json.value("start"_L1).toObject().value("date"_L1).toString(), u"2026-11-20"_s);
        QCOMPARE(json.value("end"_L1).toObject().value("date"_L1).toString(), u"2026-11-23"_s);
        // Says so when an event stops having a time of day.
        QVERIFY(json.value("start"_L1).toObject().value("dateTime"_L1).isNull());
        QVERIFY(json.value("recurrence"_L1).toArray().isEmpty());
    }

    void namesNewEventsAsGoogleWants()
    {
        const QUrl collection(u"https://example.org/calendars/a/events"_s);
        QCOMPARE(GoogleClient::hrefFor(collection, u"0B5E7C5A-1d2f-4a3b-9c8d-7e6f5a4b3c2d"_s),
                 u"https://example.org/calendars/a/events/0b5e7c5a1d2f4a3b9c8d7e6f5a4b3c2d"_s);
        const QString odd = GoogleClient::hrefFor(collection, u"Würfel@example.org"_s).section(u'/', -1);
        QVERIFY2(QRegularExpression(u"^[0-9a-v]{5,}$"_s).match(odd).hasMatch(), qPrintable(odd));
    }


    void synchronizes()
    {
        if (!m_google.start(m_dir.filePath(u"server.log"_s))) {
            QSKIP("There is no python3 to run the stand-in for Google with.");
        }
        // Already there: an event, and one that repeats with a changed appearance.
        m_google.request("PUT", u"control/ada@example.org/lunch"_s, timed(u"lunch"_s, u"Lunch"_s, u"2026-11-05T12:00:00+01:00"_s, u"2026-11-05T13:00:00+01:00"_s));
        QJsonObject standup = timed(u"standup"_s, u"Stand-up"_s, u"2026-11-02T09:00:00+01:00"_s, u"2026-11-02T09:15:00+01:00"_s);
        standup.insert("recurrence"_L1, QJsonArray{u"RRULE:FREQ=WEEKLY;COUNT=3"_s});
        m_google.request("PUT", u"control/ada@example.org/standup"_s, standup);
        QJsonObject moved = timed(u"standup_20261109T080000Z"_s, u"Stand-up (late)"_s, u"2026-11-09T11:00:00+01:00"_s, u"2026-11-09T11:15:00+01:00"_s);
        moved.insert("recurringEventId"_L1, u"standup"_s);
        moved.insert("originalStartTime"_L1, QJsonObject{{"dateTime"_L1, u"2026-11-09T09:00:00+01:00"_s}, {"timeZone"_L1, u"Europe/Berlin"_s}});
        m_google.request("PUT", u"control/ada@example.org/standup_20261109T080000Z"_s, moved);
        m_google.request("PUT", u"control/en.holidays%23holiday@group.v.calendar.google.com/h1"_s,
                         QJsonObject{{"summary"_L1, u"Holiday"_s},
                                     {"start"_L1, QJsonObject{{"date"_L1, u"2026-11-11"_s}}},
                                     {"end"_L1, QJsonObject{{"date"_L1, u"2026-11-12"_s}}}});

        Engine engine;
        engine.setUndoPeriod(0);
        QStringList notices;
        connect(&engine, &Engine::notice, this, [&](const QString &text) {
            notices.append(text);
        });
        QVERIFY(engine.open(m_dir.filePath(u"google.sqlite"_s)));
        addAccount(engine);
        sync(engine);

        // The calendar that only says when somebody is busy is left out.
        const QString ada = calendarId(engine, u"Ada"_s);
        const QString holidays = calendarId(engine, u"Holidays"_s);
        QVERIFY(!ada.isEmpty() && !holidays.isEmpty());
        QVERIFY(calendarId(engine, u"Somebody"_s).isEmpty());
        QCOMPARE(engine.collection(ada)->color, QColor(0x27, 0xae, 0x60));
        QVERIFY(!engine.collection(ada)->readOnly);
        QVERIFY(engine.collection(holidays)->readOnly);
        QCOMPARE(titles(engine), (QStringList{u"Holiday"_s, u"Lunch"_s, u"Stand-up"_s, u"Stand-up"_s, u"Stand-up (late)"_s}));

        // Made here, seen there.
        const QDateTime start(QDate(2026, 11, 12), QTime(14, 0));
        QCOMPARE(engine.saveEvent({{u"collectionId"_s, ada},
                                   {u"summary"_s, u"Review"_s},
                                   {u"location"_s, u"Room 4"_s},
                                   {u"start"_s, start},
                                   {u"end"_s, start.addSecs(1800)},
                                   {u"repeat"_s, u"none"_s}}),
                 QString());
        QTRY_COMPARE(engine.pendingChanges(), 0);
        QJsonObject review = onServer(u"Review"_s);
        QCOMPARE(review.value("location"_L1).toString(), u"Room 4"_s);
        QCOMPARE(QDateTime::fromString(review.value("start"_L1).toObject().value("dateTime"_L1).toString(), Qt::ISODate), start);
        sync(engine);
        QCOMPARE(titles(engine).count(u"Review"_s), 1);

        // Changed here, seen there; what Kalendar knows nothing of stays.
        m_google.request("PATCH", m_calendar + u"/"_s + review.value("id"_L1).toString(), QJsonObject{{"colorId"_L1, u"5"_s}});
        sync(engine);
        QVariantMap data = find(engine, u"Review"_s);
        data.insert(u"summary"_s, u"Design review"_s);
        data.insert(u"location"_s, QString());
        QCOMPARE(engine.saveEvent(data), QString());
        QTRY_COMPARE(engine.pendingChanges(), 0);
        review = onServer(u"Design review"_s);
        QCOMPARE(review.value("location"_L1).toString(), QString());
        QCOMPARE(review.value("colorId"_L1).toString(), u"5"_s);

        // Changed there, seen here.
        m_google.request("PATCH", m_calendar + u"/lunch"_s, QJsonObject{{"summary"_L1, u"Long lunch"_s}});
        sync(engine);
        QVERIFY(titles(engine).contains(u"Long lunch"_s));
        QVERIFY(!titles(engine).contains(u"Lunch"_s));

        // Changed in both places: the server's version is kept, and that is said.
        m_google.request("PATCH", m_calendar + u"/lunch"_s, QJsonObject{{"summary"_L1, u"Lunch with Grace"_s}});
        data = find(engine, u"Long lunch"_s);
        data.insert(u"summary"_s, u"Short lunch"_s);
        notices.clear();
        QCOMPARE(engine.saveEvent(data), QString());
        QTRY_COMPARE(engine.pendingChanges(), 0);
        QVERIFY(titles(engine).contains(u"Lunch with Grace"_s));
        QCOMPARE(notices.size(), 1);
        QVERIFY(!onServer(u"Lunch with Grace"_s).isEmpty());

        // One appearance of a repeating event removed here.
        data = find(engine, u"Stand-up"_s, QDate(2026, 11, 16));
        QVERIFY(!data.isEmpty());
        QVERIFY(engine.deleteEvent(data.value(u"collectionId"_s).toString(), data.value(u"href"_s).toString(), data.value(u"slot"_s).toDateTime(), false) != 0);
        QTRY_COMPARE(engine.pendingChanges(), 0);
        QCOMPARE(titles(engine).count(u"Stand-up"_s), 1);
        const QJsonArray rules = onServer(u"Stand-up"_s).value("recurrence"_L1).toArray();
        QCOMPARE(rules.size(), 2);
        QVERIFY2(rules.at(1).toString().contains("20261116"_L1), qPrintable(rules.at(1).toString()));

        // A changed appearance changed again here.
        data = find(engine, u"Stand-up (late)"_s);
        data.insert(u"summary"_s, u"Stand-up (very late)"_s);
        QCOMPARE(engine.saveEvent(data), QString());
        QTRY_COMPARE(engine.pendingChanges(), 0);
        QCOMPARE(m_google.request("GET", m_calendar + u"/standup_20261109T080000Z"_s).value("summary"_L1).toString(), u"Stand-up (very late)"_s);
        QCOMPARE(onServer(u"Stand-up"_s).value("summary"_L1).toString(), u"Stand-up"_s);
        sync(engine);
        QVERIFY(titles(engine).contains(u"Stand-up (very late)"_s));
        QCOMPARE(titles(engine).count(u"Stand-up"_s), 1);

        // Removed here, gone there.
        data = find(engine, u"Lunch with Grace"_s);
        QVERIFY(engine.deleteEvent(data.value(u"collectionId"_s).toString(), data.value(u"href"_s).toString(), {}, true) != 0);
        QTRY_COMPARE(engine.pendingChanges(), 0);
        QVERIFY(onServer(u"Lunch with Grace"_s).isEmpty());

        // Removed there, gone here.
        int status = 0;
        m_google.request("DELETE", m_calendar + u"/"_s + review.value("id"_L1).toString(), {}, &status);
        QCOMPARE(status, 204);
        sync(engine);
        QVERIFY(!titles(engine).contains(u"Design review"_s));

        // Everything is still there after a restart, without the server.
        const QStringList before = titles(engine);
        Engine later;
        QVERIFY(later.open(m_dir.filePath(u"google.sqlite"_s)));
        QCOMPARE(titles(later), before);
    }

    void saysWhenTheSignInIsNoLongerGood()
    {
        if (m_google.api().port() <= 0) {
            QSKIP("There is no python3 to run the stand-in for Google with.");
        }
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"refused.sqlite"_s)));
        Discovery found;
        bool done = false;
        engine.discoverGoogle(m_google.api(), "Bearer something-else", [&](const Discovery &discovery) {
            found = discovery;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(found.calendars.isEmpty());
        QVERIFY2(found.error.contains("Online Accounts"_L1), qPrintable(found.error));
    }
};

QTEST_GUILESS_MAIN(GoogleTest)

#include "googletest.moc"
