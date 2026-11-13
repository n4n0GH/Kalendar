// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"
#include "importsample.h"
#include "radicaleserver.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

/// Two installations of Kalendar ("here" and "there") that share an account
/// on a real CalDAV server.
class SyncTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    RadicaleServer m_radicale;
    QUrl m_address;
    const QByteArray m_authorization = "Basic " + QByteArray("ada:lovelace").toBase64();

    bool request(const QByteArray &verb, const QUrl &url, const QByteArray &body)
    {
        QNetworkAccessManager network;
        QNetworkRequest request(url);
        request.setRawHeader("Authorization", m_authorization);
        QNetworkReply *reply = network.sendCustomRequest(request, verb, body);
        QSignalSpy finished(reply, &QNetworkReply::finished);
        finished.wait(10000);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        return status >= 200 && status < 300;
    }

    Discovery discover(Engine &engine, const QUrl &address, const QByteArray &authorization)
    {
        Discovery result;
        bool done = false;
        engine.discover(address, authorization, [&](const Discovery &discovery) {
            result = discovery;
            done = true;
        });
        if (!QTest::qWaitFor([&] {
                return done;
            }, 10000)) {
            result.error = u"no answer"_s;
        }
        return result;
    }

    void addAccount(Engine &engine)
    {
        engine.setAuthProvider([this](const Account &, const Engine::AuthCallback &done) {
            done(m_authorization, {});
        });
        const Discovery found = discover(engine, m_address, m_authorization);
        QCOMPARE(found.error, QString());
        Account account;
        account.type = Account::CalDav;
        account.name = u"ada"_s;
        account.user = u"ada"_s;
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

    static QString workId(const Engine &engine)
    {
        for (const Collection &collection : engine.collections()) {
            if (collection.name == "Work"_L1) {
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

    static QVariantMap find(const Engine &engine, const QString &title)
    {
        const QVariantList segments = engine.weekLayout(QDate(2026, 11, 1), 30, false, 0).value(u"segments"_s).toList();
        for (const QVariant &segment : segments) {
            if (segment.toMap().value(u"title"_s).toString() == title) {
                QVariantMap data = segment.toMap();
                data.insert(u"originalCollectionId"_s, data.value(u"collectionId"_s));
                return data;
            }
        }
        return {};
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!m_radicale.start(m_dir.path())) {
            QSKIP("Radicale is not installed; set KALENDAR_RADICALE to a Python that has it.");
        }
        m_address = m_radicale.address();

        // Radicale creates the user's folder at the first visit; the calendars are made here.
        request("PROPFIND", m_address.resolved(QUrl(u"/ada/"_s)), {});
        QVERIFY(request("MKCALENDAR", m_address.resolved(QUrl(u"/ada/work/"_s)),
                        "<?xml version=\"1.0\"?><c:mkcalendar xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
                        "xmlns:a=\"http://apple.com/ns/ical/\"><d:set><d:prop><d:displayname>Work</d:displayname>"
                        "<a:calendar-color>#27ae60ff</a:calendar-color>"
                        "<c:supported-calendar-component-set><c:comp name=\"VEVENT\"/></c:supported-calendar-component-set>"
                        "</d:prop></d:set></c:mkcalendar>"));
        QVERIFY(request("MKCALENDAR", m_address.resolved(QUrl(u"/ada/tasks/"_s)),
                        "<?xml version=\"1.0\"?><c:mkcalendar xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
                        "<d:set><d:prop><d:displayname>Tasks</d:displayname>"
                        "<c:supported-calendar-component-set><c:comp name=\"VTODO\"/></c:supported-calendar-component-set>"
                        "</d:prop></d:set></c:mkcalendar>"));
    }

    void findsCalendarsFromTheServerName()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"discover.sqlite"_s)));
        const Discovery found = discover(engine, m_address, m_authorization);
        QCOMPARE(found.error, QString());
        QCOMPARE(found.home, m_address.resolved(QUrl(u"/ada/"_s)));
        // The task list is not a calendar for events.
        QCOMPARE(found.calendars.size(), 1);
        QCOMPARE(found.calendars.first().name, u"Work"_s);
        QCOMPARE(found.calendars.first().color, QColor(0x27, 0xae, 0x60));
        QVERIFY(!found.calendars.first().readOnly);

        // Also from the address of the calendar itself.
        QCOMPARE(discover(engine, m_address.resolved(QUrl(u"/ada/work/"_s)), m_authorization).calendars.size(), 1);
    }

    void saysWhenThePasswordIsWrong()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"discover.sqlite"_s)));
        const Discovery found = discover(engine, m_address, "Basic " + QByteArray("ada:babbage").toBase64());
        QVERIFY(found.calendars.isEmpty());
        QVERIFY2(found.error.contains("password"_L1), qPrintable(found.error));
    }

    void saysWhenThereIsNoServer()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"discover.sqlite"_s)));
        const Discovery found = discover(engine, QUrl(u"http://127.0.0.1:9"_s), m_authorization);
        QVERIFY(found.calendars.isEmpty());
        QVERIFY2(found.error.contains("could not be reached"_L1), qPrintable(found.error));
    }

    void keepsTwoSystemsInStep()
    {
        Engine here;
        Engine there;
        here.setUndoPeriod(0);
        QVERIFY(here.open(m_dir.filePath(u"here.sqlite"_s)));
        QVERIFY(there.open(m_dir.filePath(u"there.sqlite"_s)));
        addAccount(here);
        addAccount(there);
        QCOMPARE(here.collections().size(), 2);
        QVERIFY(!workId(here).isEmpty());

        // Made here, seen there.
        const QDateTime start(QDate(2026, 11, 12), QTime(14, 0));
        QCOMPARE(here.saveEvent({{u"collectionId"_s, workId(here)},
                                 {u"summary"_s, u"Review"_s},
                                 {u"location"_s, u"Room 4; second floor"_s},
                                 {u"start"_s, start},
                                 {u"end"_s, start.addSecs(1800)},
                                 {u"repeat"_s, u"none"_s}}),
                 QString());
        QCOMPARE(titles(here), QStringList{u"Review"_s});
        QTRY_COMPARE(here.pendingChanges(), 0);
        sync(there);
        QCOMPARE(titles(there), QStringList{u"Review"_s});
        QVariantMap review = find(there, u"Review"_s);
        QCOMPARE(review.value(u"start"_s).toDateTime(), start);
        QCOMPARE(review.value(u"location"_s).toString(), u"Room 4; second floor"_s);

        // Changed there, seen here.
        review.insert(u"summary"_s, u"Design review"_s);
        QCOMPARE(there.saveEvent(review), QString());
        QTRY_COMPARE(there.pendingChanges(), 0);
        sync(here);
        QCOMPARE(titles(here), QStringList{u"Design review"_s});

        // Changed again right away: the version mark from the last save must still be good.
        review = find(there, u"Design review"_s);
        review.insert(u"summary"_s, u"Design review 2"_s);
        QCOMPARE(there.saveEvent(review), QString());
        QTRY_COMPARE(there.pendingChanges(), 0);
        QCOMPARE(titles(there), QStringList{u"Design review 2"_s});
        sync(here);
        QCOMPARE(titles(here), QStringList{u"Design review 2"_s});

        // Changed on both sides: the one that reaches the server first wins, and the other is told.
        review = find(there, u"Design review 2"_s);
        review.insert(u"summary"_s, u"From there"_s);
        QCOMPARE(there.saveEvent(review), QString());
        QTRY_COMPARE(there.pendingChanges(), 0);
        QSignalSpy notices(&here, &Engine::notice);
        review = find(here, u"Design review 2"_s);
        review.insert(u"summary"_s, u"From here"_s);
        QCOMPARE(here.saveEvent(review), QString());
        QTRY_COMPARE(here.pendingChanges(), 0);
        QCOMPARE(titles(here), QStringList{u"From there"_s});
        QCOMPARE(notices.size(), 1);

        // A second event; a sync that has nothing to do changes nothing.
        QCOMPARE(here.saveEvent({{u"collectionId"_s, workId(here)},
                                 {u"summary"_s, u"Retreat"_s},
                                 {u"start"_s, QDateTime(QDate(2026, 11, 20), QTime(0, 0))},
                                 {u"end"_s, QDateTime(QDate(2026, 11, 22), QTime(0, 0))},
                                 {u"allDay"_s, true},
                                 {u"repeat"_s, u"yearly"_s}}),
                 QString());
        QTRY_COMPARE(here.pendingChanges(), 0);
        sync(there);
        sync(there);
        QCOMPARE(titles(there), QStringList({u"From there"_s, u"Retreat"_s}));
        const QVariantMap retreat = find(there, u"Retreat"_s);
        QVERIFY(retreat.value(u"allDay"_s).toBool());
        QCOMPARE(retreat.value(u"end"_s).toDateTime().date(), QDate(2026, 11, 22));
        QCOMPARE(retreat.value(u"repeat"_s).toString(), u"yearly"_s);
        QCOMPARE(there.occurrences(QDate(2027, 11, 21), QDate(2027, 11, 21)).size(), 1);

        // Deleted here, gone there.
        review = find(here, u"From there"_s);
        QVERIFY(here.deleteEvent(review.value(u"collectionId"_s).toString(), review.value(u"href"_s).toString(), {}, true) != 0);
        QCOMPARE(titles(here), QStringList{u"Retreat"_s});
        QTRY_COMPARE(here.pendingChanges(), 0);
        sync(there);
        QCOMPARE(titles(there), QStringList{u"Retreat"_s});
    }

    void sendsLaterWhatCouldNotBeSent()
    {
        Engine here;
        QVERIFY(here.open(m_dir.filePath(u"offline.sqlite"_s)));
        addAccount(here);
        // The password stops working, as if the network were gone.
        bool online = false;
        here.setAuthProvider([&](const Account &, const Engine::AuthCallback &done) {
            done(online ? m_authorization : QByteArray("Basic bm86bm8="), {});
        });
        const QDateTime start(QDate(2026, 11, 5), QTime(8, 0));
        QCOMPARE(here.saveEvent({{u"collectionId"_s, workId(here)},
                                 {u"summary"_s, u"Written offline"_s},
                                 {u"start"_s, start},
                                 {u"end"_s, start.addSecs(1800)},
                                 {u"repeat"_s, u"none"_s}}),
                 QString());
        QTRY_VERIFY(!here.accountError(here.accounts().last().id).isEmpty());
        QCOMPARE(here.pendingChanges(), 1);
        QVERIFY(titles(here).contains(u"Written offline"_s));

        online = true;
        sync(here);
        QCOMPARE(here.pendingChanges(), 0);

        Engine there;
        QVERIFY(there.open(m_dir.filePath(u"offline-there.sqlite"_s)));
        addAccount(there);
        QVERIFY(titles(there).contains(u"Written offline"_s));
    }

    void sendsWhatWasImported()
    {
        Engine here;
        QVERIFY(here.open(m_dir.filePath(u"import.sqlite"_s)));
        addAccount(here);
        int imported = 0;
        QCOMPARE(here.importEvents(workId(here), ImportSample, &imported), QString());
        QCOMPARE(imported, 2);
        QTRY_COMPARE_WITH_TIMEOUT(here.pendingChanges(), 0, 10000);
        QCOMPARE(here.accountError(here.accounts().last().id), QString());

        // The server has them, the exception to the repetition included.
        Engine there;
        QVERIFY(there.open(m_dir.filePath(u"import-there.sqlite"_s)));
        addAccount(there);
        QVERIFY(titles(there).contains(u"Fair"_s));
        QVERIFY(titles(there).contains(u"Call with New York (late)"_s));
        const qsizetype before = there.occurrences(QDate(2026, 11, 1), QDate(2026, 12, 31)).size();

        // Imported again on the other system, they replace what is there.
        QCOMPARE(there.importEvents(workId(there), ImportSample), QString());
        QTRY_COMPARE_WITH_TIMEOUT(there.pendingChanges(), 0, 10000);
        sync(there);
        QCOMPARE(there.occurrences(QDate(2026, 11, 1), QDate(2026, 12, 31)).size(), before);
    }
};

QTEST_GUILESS_MAIN(SyncTest)

#include "synctest.moc"
