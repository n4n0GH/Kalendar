// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "onlineaccounts.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>

using namespace Qt::StringLiterals;

namespace
{
const QString Service = u"com.google.code.AccountsSSO.SingleSignOn"_s;
const QString ServicePath = u"/com/google/code/AccountsSSO/SingleSignOn"_s;
/// Renewing a token takes a request to Google.
constexpr int TokenTimeout = 60000;
/// signond: never show a window.
constexpr quint32 NoUserInteraction = 2;

struct Row {
    QString key;
    QString type;
    QString value;
};

/// Runs @p sql on the database at @p path, which belongs to another program
/// and is only looked at.
QList<QVariantList> query(const QString &path, const QString &sql, const QVariantList &values = {})
{
    QList<QVariantList> rows;
    if (!QFile::exists(path)) {
        return rows;
    }
    // Reading alone fails where the folder cannot be written to and nobody
    // else has the database open; then it is safe to read it as it lies.
    for (const QString &options : {u"mode=ro"_s, u"immutable=1"_s}) {
        const QString name = u"kalendar-accounts-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool ok = false;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(u"QSQLITE"_s, name);
            db.setConnectOptions(u"QSQLITE_OPEN_READONLY;QSQLITE_OPEN_URI;QSQLITE_BUSY_TIMEOUT=2000"_s);
            db.setDatabaseName(u"file:"_s + QString::fromLatin1(QUrl::toPercentEncoding(path, "/")) + u'?' + options);
            if (db.open()) {
                QSqlQuery statement(db);
                statement.prepare(sql);
                for (const QVariant &value : values) {
                    statement.addBindValue(value);
                }
                ok = statement.exec();
                while (ok && statement.next()) {
                    QVariantList row;
                    for (int column = 0; column < statement.record().count(); ++column) {
                        row.append(statement.value(column));
                    }
                    rows.append(row);
                }
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        if (ok) {
            break;
        }
    }
    return rows;
}

/// Reads one piece of quoted text starting at @p pos, and moves @p pos behind it.
QString readQuoted(const QString &text, qsizetype &pos)
{
    QString result;
    const QChar quote = text.at(pos++);
    while (pos < text.size() && text.at(pos) != quote) {
        QChar c = text.at(pos++);
        if (c == u'\\' && pos < text.size()) {
            c = text.at(pos++);
            if (c == u'n') {
                c = u'\n';
            } else if (c == u't') {
                c = u'\t';
            } else if (c == u'u' && pos + 4 <= text.size()) {
                c = QChar(text.mid(pos, 4).toUShort(nullptr, 16));
                pos += 4;
            }
        }
        result += c;
    }
    ++pos;
    return result;
}

/// The settings of the "auth" part of the description of the Google
/// accounts, for accounts that have none of their own.
QList<Row> providerSettings()
{
    QStringList files = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, u"accounts/providers/kde/google.provider"_s);
    files += QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, u"accounts/providers/google.provider"_s);
    // Inside a Flatpak, the system's files are here, if it may see them.
    files += u"/run/host/usr/share/accounts/providers/kde/google.provider"_s;
    QList<Row> rows;
    for (const QString &name : std::as_const(files)) {
        QFile file(name);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QXmlStreamReader xml(&file);
        QStringList groups;
        while (!xml.atEnd()) {
            const auto token = xml.readNext();
            if (token == QXmlStreamReader::StartElement && xml.name() == "group"_L1) {
                groups.append(xml.attributes().value("name"_L1).toString());
            } else if (token == QXmlStreamReader::EndElement && xml.name() == "group"_L1) {
                groups.removeLast();
            } else if (token == QXmlStreamReader::StartElement && xml.name() == "setting"_L1) {
                const QString type = xml.attributes().value("type"_L1).toString();
                const QString key = (groups + QStringList{xml.attributes().value("name"_L1).toString()}).join(u'/');
                const QString value = xml.readElementText().trimmed();
                // Text is written down without quotes here.
                rows.append({key, type.isEmpty() ? u"-"_s : type, value});
            }
        }
        if (!rows.isEmpty()) {
            break;
        }
    }
    return rows;
}

QString describe(const QDBusError &error)
{
    const QString name = error.name();
    if (name.contains("ServiceUnknown"_L1) || name.contains("NameHasNoOwner"_L1) || name.contains("AccessDenied"_L1)
        || name.contains("Spawn"_L1)) {
        return i18nc("@info", "The Online Accounts of this system cannot be reached.");
    }
    if (name.contains("UserInteraction"_L1) || name.contains("NotAuthorized"_L1) || name.contains("InvalidCredentials"_L1)
        || name.contains("IdentityNotFound"_L1) || name.contains("Forbidden"_L1) || name.contains("PermissionDenied"_L1)) {
        return i18nc("@info", "Google wants you to sign in again. This is done under Online Accounts in System Settings.");
    }
    return i18nc("@info %1 is a technical reason", "Online Accounts could not sign in: %1", error.message().isEmpty() ? name : error.message());
}
}

OnlineAccounts::OnlineAccounts(bool testMode, QObject *parent)
    : QObject(parent)
    , m_testMode(testMode)
{
}

QString OnlineAccounts::databasePath() const
{
    if (qEnvironmentVariableIsSet("KALENDAR_ACCOUNTS_DATABASE")) {
        return qEnvironmentVariable("KALENDAR_ACCOUNTS_DATABASE");
    }
    // Inside a Flatpak the settings folder is the app's own; the system's is named here.
    QString config = qEnvironmentVariable("HOST_XDG_CONFIG_HOME");
    if (config.isEmpty()) {
        config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    }
    return config + u"/libaccounts-glib/accounts.db"_s;
}

QVariant OnlineAccounts::parseValue(const QString &type, const QString &text)
{
    const QString value = text.trimmed();
    if (type == "-"_L1) {
        return value;
    }
    if (type == "s"_L1) {
        qsizetype pos = 0;
        return value.startsWith(u'\'') || value.startsWith(u'"') ? readQuoted(value, pos) : value;
    }
    if (type == "b"_L1) {
        return value == "true"_L1;
    }
    if (type == "as"_L1) {
        QStringList list;
        for (qsizetype pos = 0; pos < value.size();) {
            if (value.at(pos) == u'\'' || value.at(pos) == u'"') {
                list.append(readQuoted(value, pos));
            } else {
                ++pos;
            }
        }
        return list;
    }
    // Numbers may come with their type in front, as in "uint32 7".
    const QString number = value.section(u' ', -1);
    if (type == "u"_L1 || type == "q"_L1 || type == "y"_L1) {
        return number.toUInt();
    }
    if (type == "i"_L1 || type == "n"_L1) {
        return number.toInt();
    }
    if (type == "x"_L1) {
        return number.toLongLong();
    }
    if (type == "t"_L1) {
        return number.toULongLong();
    }
    if (type == "d"_L1) {
        return number.toDouble();
    }
    return {};
}

QList<OnlineAccounts::Entry> OnlineAccounts::googleAccounts() const
{
    QList<Entry> entries;
    if (m_testMode) {
        const QStringList named = qEnvironmentVariable("KALENDAR_ONLINE_ACCOUNTS").split(u',', Qt::SkipEmptyParts);
        for (const QString &one : named) {
            entries.append({one.section(u'=', 0, 0).toUInt(), one.section(u'=', 1)});
        }
        return entries;
    }
    const auto rows = query(databasePath(), u"SELECT id, name FROM Accounts WHERE provider = 'google' AND enabled = 1 ORDER BY id"_s);
    for (const QVariantList &row : rows) {
        Entry entry;
        entry.id = row.value(0).toUInt();
        entry.name = row.value(1).toString();
        if (entry.name.isEmpty()) {
            entry.name = i18nc("@title name of an account that has none", "Google");
        }
        entries.append(entry);
    }
    return entries;
}

QVariantMap OnlineAccounts::signInSettings(quint32 id, quint32 *credentials, QString *method, QString *mechanism) const
{
    QList<Row> rows;
    // An account's own settings come last, so that they win.
    const auto stored = query(databasePath(), u"SELECT key, type, value FROM Settings WHERE account = ? ORDER BY service DESC"_s, {id});
    for (const QVariantList &row : stored) {
        rows.append({row.value(0).toString(), row.value(1).toString(), QString::fromUtf8(row.value(2).toByteArray())});
    }
    QVariantMap all;
    for (const auto &source : {providerSettings(), rows}) {
        for (const Row &row : source) {
            all.insert(row.key, parseValue(row.type, row.value));
        }
    }
    *credentials = all.value(u"CredentialsId"_s).toUInt();
    *method = all.value(u"auth/method"_s).toString();
    *mechanism = all.value(u"auth/mechanism"_s).toString();
    const QString prefix = u"auth/"_s + *method + u'/' + *mechanism + u'/';
    QVariantMap settings;
    for (auto it = all.cbegin(); it != all.cend(); ++it) {
        if (it.key().startsWith(prefix)) {
            settings.insert(it.key().mid(prefix.size()), it.value());
        }
    }
    return settings;
}

void OnlineAccounts::token(quint32 id, const TokenCallback &done)
{
    if (m_testMode) {
        QTimer::singleShot(0, this, [id, done] {
            done(u"test-token-%1"_s.arg(id), 3600, {});
        });
        return;
    }
    quint32 credentials = 0;
    QString method;
    QString mechanism;
    QVariantMap settings = signInSettings(id, &credentials, &method, &mechanism);
    if (credentials == 0 || method.isEmpty() || settings.isEmpty()) {
        QTimer::singleShot(0, this, [done] {
            done({}, 0, i18nc("@info", "The account is no longer set up under Online Accounts in System Settings."));
        });
        return;
    }
    settings.insert(u"UiPolicy"_s, NoUserInteraction);

    QDBusMessage open = QDBusMessage::createMethodCall(Service, ServicePath, Service + u".AuthService"_s, u"getAuthSessionObjectPath"_s);
    open.setArguments({credentials, QString(), method});
    auto *opening = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(open), this);
    connect(opening, &QDBusPendingCallWatcher::finished, this, [=, this] {
        opening->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> session = *opening;
        if (session.isError()) {
            done({}, 0, describe(session.error()));
            return;
        }
        const QString path = session.value().path();
        QDBusMessage process = QDBusMessage::createMethodCall(Service, path, Service + u".AuthSession"_s, u"process"_s);
        process.setArguments({settings, mechanism});
        auto *processing = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(process, TokenTimeout), this);
        connect(processing, &QDBusPendingCallWatcher::finished, this, [=] {
            processing->deleteLater();
            // The service keeps the session until told that it is done with.
            QDBusConnection::sessionBus().send(QDBusMessage::createMethodCall(Service, path, Service + u".AuthSession"_s, u"objectUnref"_s));
            const QDBusPendingReply<QVariantMap> answer = *processing;
            if (answer.isError()) {
                done({}, 0, describe(answer.error()));
                return;
            }
            const QVariantMap result = answer.value();
            const QString token = result.value(u"AccessToken"_s).toString();
            if (token.isEmpty()) {
                done({}, 0, i18nc("@info", "Google wants you to sign in again. This is done under Online Accounts in System Settings."));
                return;
            }
            done(token, result.value(u"ExpiresIn"_s).toInt(), {});
        });
    });
}
