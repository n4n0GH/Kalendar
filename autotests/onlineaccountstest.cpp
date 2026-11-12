// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine.h"
#include "googleclient.h"
#include "onlineaccounts.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

/// The accounts of the system's Online Accounts.
class OnlineAccountsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;

private Q_SLOTS:
    void readsValuesAsWrittenDown()
    {
        QCOMPARE(OnlineAccounts::parseValue(u"s"_s, u"'o/oauth2/token'"_s), QVariant(u"o/oauth2/token"_s));
        QCOMPARE(OnlineAccounts::parseValue(u"s"_s, u"\"Ada's\""_s), QVariant(u"Ada's"_s));
        QCOMPARE(OnlineAccounts::parseValue(u"s"_s, u"'a\\\\b\\'c'"_s), QVariant(u"a\\b'c"_s));
        QCOMPARE(OnlineAccounts::parseValue(u"b"_s, u"true"_s), QVariant(true));
        QCOMPARE(OnlineAccounts::parseValue(u"b"_s, u"false"_s), QVariant(false));
        QCOMPARE(OnlineAccounts::parseValue(u"u"_s, u"7"_s), QVariant(7u));
        QCOMPARE(OnlineAccounts::parseValue(u"u"_s, u"uint32 7"_s), QVariant(7u));
        QCOMPARE(OnlineAccounts::parseValue(u"as"_s, u"['https://a/b', 'c, d']"_s), QVariant(QStringList{u"https://a/b"_s, u"c, d"_s}));
        QCOMPARE(OnlineAccounts::parseValue(u"as"_s, u"@as []"_s), QVariant(QStringList()));
    }

    void listsTheGoogleAccountsThatAreSwitchedOn()
    {
        const QString path = m_dir.filePath(u"accounts.db"_s);
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, u"setup"_s);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(u"CREATE TABLE Accounts (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT, provider TEXT, enabled INTEGER)"_s));
            QVERIFY(query.exec(u"INSERT INTO Accounts (name, provider, enabled) VALUES ('Private', 'google', 1), ('Cloud', 'nextcloud', 1), "
                               "('Old', 'google', 0), ('Work', 'google', 1)"_s));
            db.close();
        }
        QSqlDatabase::removeDatabase(u"setup"_s);

        qputenv("KALENDAR_ACCOUNTS_DATABASE", path.toUtf8());
        const OnlineAccounts accounts(false);
        const QList<OnlineAccounts::Entry> found = accounts.googleAccounts();
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.at(0).id, 1u);
        QCOMPARE(found.at(0).name, u"Private"_s);
        QCOMPARE(found.at(1).id, 4u);
        QCOMPARE(found.at(1).name, u"Work"_s);

        // No such database: no accounts, and no complaint.
        qputenv("KALENDAR_ACCOUNTS_DATABASE", m_dir.filePath(u"none.db"_s).toUtf8());
        QVERIFY(accounts.googleAccounts().isEmpty());
        qunsetenv("KALENDAR_ACCOUNTS_DATABASE");
    }

    /// Looks at the calendars of the Google accounts this system is really
    /// signed in to, changing nothing. Only when asked for:
    /// KALENDAR_LIVE_GOOGLE=1.
    void readsTheRealThing()
    {
        if (!qEnvironmentVariableIsSet("KALENDAR_LIVE_GOOGLE")) {
            QSKIP("Set KALENDAR_LIVE_GOOGLE=1 to read the calendars of this system's Google accounts.");
        }
        OnlineAccounts accounts(false);
        const QList<OnlineAccounts::Entry> found = accounts.googleAccounts();
        QVERIFY2(!found.isEmpty(), "No Google account under Online Accounts.");

        QString token;
        QString error;
        bool done = false;
        accounts.token(found.first().id, [&](const QString &value, int expiresIn, const QString &why) {
            token = value;
            error = why;
            done = true;
            qInfo("token good for %d s", expiresIn);
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 70000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!token.isEmpty());

        Engine engine;
        QVERIFY(engine.open(m_dir.filePath(u"live.sqlite"_s)));
        engine.setAuthProvider([&](const Account &, const Engine::AuthCallback &callback) {
            callback("Bearer " + token.toLatin1(), {});
        });
        Discovery discovery;
        done = false;
        engine.discoverGoogle(GoogleClient::api(), "Bearer " + token.toLatin1(), [&](const Discovery &result) {
            discovery = result;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 40000);
        QVERIFY2(discovery.error.isEmpty(), qPrintable(discovery.error));
        Account account;
        account.type = Account::Google;
        account.name = found.first().name;
        account.user = QString::number(found.first().id);
        account.url = discovery.home;
        const QString id = engine.addAccount(account, discovery.calendars);
        QTRY_VERIFY_WITH_TIMEOUT(!engine.isSyncing(), 120000);
        QVERIFY2(engine.accountError(id).isEmpty(), qPrintable(engine.accountError(id)));
        const QDate today = QDate::currentDate();
        qInfo("%d calendars (%d of them read-only), %d appearances of events within a year of today",
              int(discovery.calendars.size()),
              int(std::count_if(discovery.calendars.cbegin(), discovery.calendars.cend(), [](const CalendarInfo &info) {
                  return info.readOnly;
              })),
              int(engine.occurrences(today.addDays(-365), today.addDays(365)).size()));
        QCOMPARE(engine.pendingChanges(), 0);
    }
};

QTEST_GUILESS_MAIN(OnlineAccountsTest)

#include "onlineaccountstest.moc"
