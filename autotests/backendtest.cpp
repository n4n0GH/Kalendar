// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "backend.h"
#include "radicaleserver.h"
#include "secretstore.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

/// What the interface does to add and remove an account, with a real server
/// and with the passwords kept in memory instead of the wallet.
class BackendTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    RadicaleServer m_radicale;

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (!m_radicale.start(m_dir.path())) {
            QSKIP("Radicale is not installed; set KALENDAR_RADICALE to a Python that has it.");
        }
        QNetworkAccessManager network;
        QNetworkRequest request(m_radicale.address().resolved(QUrl(u"/ada/home/"_s)));
        request.setRawHeader("Authorization", "Basic " + QByteArray("ada:lovelace").toBase64());
        QNetworkReply *reply = network.sendCustomRequest(request, "MKCALENDAR");
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(10000));
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 201);
        reply->deleteLater();
    }

    void addsUsesAndRemovesAnAccount()
    {
        Engine engine;
        engine.setUndoPeriod(0);
        QVERIFY(engine.open(m_dir.filePath(u"backend.sqlite"_s)));
        SecretStore secrets(true);
        Backend backend(&engine, &secrets);
        QSignalSpy added(&backend, &Backend::accountAdded);
        QSignalSpy failed(&backend, &Backend::accountFailed);
        QCOMPARE(backend.accounts().size(), 1);
        QCOMPARE(backend.writableCalendars().size(), 1);

        // Not an address at all.
        backend.addCalDavAccount(u"   "_s, u"ada"_s, u"lovelace"_s);
        QCOMPARE(failed.size(), 1);
        QVERIFY(!backend.isAdding());

        // The wrong password.
        backend.addCalDavAccount(m_radicale.address().toString(), u"ada"_s, u"babbage"_s);
        QVERIFY(backend.isAdding());
        QTRY_COMPARE(failed.size(), 2);
        QVERIFY2(failed.last().first().toString().contains("password"_L1), qPrintable(failed.last().first().toString()));
        QVERIFY(!backend.isAdding());
        QCOMPARE(backend.accounts().size(), 1);

        // Given up before the server answered: nothing is added, nothing is reported.
        backend.addCalDavAccount(m_radicale.address().toString(), u"ada"_s, u"lovelace"_s);
        backend.cancelAdding();
        QTest::qWait(1000);
        QCOMPARE(added.size(), 0);
        QCOMPARE(failed.size(), 2);
        QCOMPARE(backend.accounts().size(), 1);

        // And right.
        backend.addCalDavAccount(u" "_s + m_radicale.address().toString() + u" "_s, u" ada "_s, u"lovelace"_s);
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(added.first().first().toString(), u"ada@127.0.0.1"_s);
        QCOMPARE(backend.accounts().size(), 2);
        const QVariantMap account = backend.accounts().last().toMap();
        QCOMPARE(account.value(u"kind"_s).toString(), u"caldav"_s);
        QVERIFY(account.value(u"removable"_s).toBool());
        QCOMPARE(backend.calendars()->rowCount(), 2);
        QCOMPARE(backend.writableCalendars().size(), 2);
        QTRY_VERIFY(!backend.isSyncing());
        QCOMPARE(backend.errorText(), QString());

        // The same account again is refused.
        backend.addCalDavAccount(m_radicale.address().toString(), u"ada"_s, u"lovelace"_s);
        QTRY_COMPARE(failed.size(), 3);
        QCOMPARE(backend.accounts().size(), 2);

        // An event in the server's calendar reaches the server, with the password from the store.
        const QString calendar = backend.writableCalendars().last().toMap().value(u"id"_s).toString();
        const QDateTime start(QDate(2026, 11, 12), QTime(14, 0));
        QCOMPARE(backend.saveEvent({{u"collectionId"_s, calendar}, {u"summary"_s, u"Sent"_s}, {u"start"_s, start}, {u"end"_s, start.addSecs(600)},
                                    {u"repeat"_s, u"none"_s}}),
                 QString());
        QTRY_COMPARE(engine.pendingChanges(), 0);
        QCOMPARE(backend.errorText(), QString());
        // New events go to the calendar used last.
        QCOMPARE(backend.defaultCalendarId(), calendar);
        QCOMPARE(backend.dayLayout(start.date(), 0).size(), 1);

        // Deleting, and taking it back.
        const QVariantMap event = backend.dayLayout(start.date(), 0).first().toMap();
        const int token = backend.deleteEvent(event, true);
        QVERIFY(token != 0);
        QVERIFY(backend.dayLayout(start.date(), 0).isEmpty());
        backend.undo(token);
        QCOMPARE(backend.dayLayout(start.date(), 0).size(), 1);
        QTRY_COMPARE(engine.pendingChanges(), 0);

        backend.removeAccount(account.value(u"id"_s).toString());
        QCOMPARE(backend.accounts().size(), 1);
        QCOMPARE(backend.calendars()->rowCount(), 1);
        QVERIFY(backend.dayLayout(start.date(), 0).isEmpty());
        QVERIFY(backend.defaultCalendarId() != calendar);
    }

    void readsTimesAsTyped()
    {
        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"times.sqlite"_s)));
        SecretStore secrets(true);
        Backend backend(&engine, &secrets);
        const QDateTime day(QDate(2026, 11, 12), QTime(8, 0));
        QCOMPARE(backend.parseTime(u"9:30"_s, day), QDateTime(day.date(), QTime(9, 30)));
        QCOMPARE(backend.parseTime(u" 17 "_s, day), QDateTime(day.date(), QTime(17, 0)));
        QCOMPARE(backend.parseTime(u"930"_s, day), QDateTime(day.date(), QTime(9, 30)));
        QCOMPARE(backend.parseTime(u"2 PM"_s, day), QDateTime(day.date(), QTime(14, 0)));
        QCOMPARE(backend.parseTime(u"2:15pm"_s, day), QDateTime(day.date(), QTime(14, 15)));
        QVERIFY(!backend.parseTime(u"soon"_s, day).isValid());
        QVERIFY(!backend.parseTime(u"25:00"_s, day).isValid());
    }
};

QTEST_MAIN(BackendTest)

#include "backendtest.moc"
