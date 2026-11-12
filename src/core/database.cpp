// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "database.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

using namespace Qt::StringLiterals;

Database::Database()
    : m_connection(u"kalendar-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

Database::~Database()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connection);
    }
}

QString Database::errorString() const
{
    return m_error;
}

bool Database::run(const QString &sql, const QVariantList &values) const
{
    QSqlQuery query(m_db);
    query.prepare(sql);
    for (const QVariant &value : values) {
        // A string without content is still a string, not "no value".
        const bool emptyText = value.typeId() == QMetaType::QString && value.toString().isEmpty();
        query.addBindValue(emptyText ? QVariant(QString::fromLatin1("")) : value);
    }
    if (!query.exec()) {
        m_error = query.lastError().text();
        qWarning("Database: %s (%s)", qPrintable(m_error), qPrintable(sql));
        return false;
    }
    return true;
}

bool Database::open(const QString &path, bool readOnly)
{
    m_db = QSqlDatabase::addDatabase(u"QSQLITE"_s, m_connection);
    m_db.setDatabaseName(path);
    if (readOnly) {
        m_db.setConnectOptions(u"QSQLITE_OPEN_READONLY"_s);
    }
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return false;
    }
    if (readOnly) {
        // Proves that it is a database of ours, which opening alone does not.
        return run(u"SELECT count(*) FROM items"_s);
    }
    return run(u"PRAGMA journal_mode = WAL"_s)
        && run(u"CREATE TABLE IF NOT EXISTS accounts (id TEXT PRIMARY KEY, type INTEGER NOT NULL, name TEXT NOT NULL, url TEXT NOT NULL, user TEXT NOT NULL)"_s)
        && run(u"CREATE TABLE IF NOT EXISTS collections (id TEXT PRIMARY KEY, account TEXT NOT NULL, url TEXT NOT NULL, name TEXT NOT NULL, "
               "color TEXT NOT NULL, ctag TEXT NOT NULL, enabled INTEGER NOT NULL, readonly INTEGER NOT NULL)"_s)
        && run(u"CREATE TABLE IF NOT EXISTS items (collection TEXT NOT NULL, href TEXT NOT NULL, etag TEXT NOT NULL, ics BLOB NOT NULL, "
               "dirty INTEGER NOT NULL, PRIMARY KEY (collection, href))"_s);
}

QList<Account> Database::accounts() const
{
    QList<Account> result;
    QSqlQuery query(u"SELECT id, type, name, url, user FROM accounts ORDER BY rowid"_s, m_db);
    while (query.next()) {
        Account account;
        account.id = query.value(0).toString();
        account.type = Account::Type(query.value(1).toInt());
        account.name = query.value(2).toString();
        account.url = QUrl(query.value(3).toString());
        account.user = query.value(4).toString();
        result.append(account);
    }
    return result;
}

void Database::saveAccount(const Account &account)
{
    run(u"INSERT INTO accounts (id, type, name, url, user) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT (id) DO UPDATE SET type = excluded.type, name = excluded.name, url = excluded.url, user = excluded.user"_s,
        {account.id, int(account.type), account.name, account.url.toString(), account.user});
}

void Database::removeAccount(const QString &id)
{
    run(u"DELETE FROM items WHERE collection IN (SELECT id FROM collections WHERE account = ?)"_s, {id});
    run(u"DELETE FROM collections WHERE account = ?"_s, {id});
    run(u"DELETE FROM accounts WHERE id = ?"_s, {id});
}

QList<Collection> Database::collections() const
{
    QList<Collection> result;
    QSqlQuery query(u"SELECT id, account, url, name, color, ctag, enabled, readonly FROM collections ORDER BY rowid"_s, m_db);
    while (query.next()) {
        Collection collection;
        collection.id = query.value(0).toString();
        collection.accountId = query.value(1).toString();
        collection.url = QUrl(query.value(2).toString());
        collection.name = query.value(3).toString();
        collection.color = QColor(query.value(4).toString());
        collection.ctag = query.value(5).toString();
        collection.enabled = query.value(6).toBool();
        collection.readOnly = query.value(7).toBool();
        result.append(collection);
    }
    return result;
}

void Database::saveCollection(const Collection &collection)
{
    run(u"INSERT INTO collections (id, account, url, name, color, ctag, enabled, readonly) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT (id) DO UPDATE SET account = excluded.account, url = excluded.url, name = excluded.name, color = excluded.color, "
        "ctag = excluded.ctag, enabled = excluded.enabled, readonly = excluded.readonly"_s,
        {collection.id,
         collection.accountId,
         collection.url.toString(),
         collection.name,
         collection.color.name(),
         collection.ctag,
         collection.enabled,
         collection.readOnly});
}

void Database::removeCollection(const QString &id)
{
    run(u"DELETE FROM items WHERE collection = ?"_s, {id});
    run(u"DELETE FROM collections WHERE id = ?"_s, {id});
}

QList<ItemRecord> Database::items(const QString &collectionId) const
{
    QList<ItemRecord> result;
    QSqlQuery query(m_db);
    query.prepare(u"SELECT href, etag, ics, dirty FROM items WHERE collection = ?"_s);
    query.addBindValue(collectionId);
    query.exec();
    while (query.next()) {
        ItemRecord item;
        item.collectionId = collectionId;
        item.href = query.value(0).toString();
        item.etag = query.value(1).toString();
        item.ics = query.value(2).toByteArray();
        item.dirty = query.value(3).toInt();
        result.append(item);
    }
    return result;
}

void Database::saveItem(const ItemRecord &item)
{
    run(u"INSERT INTO items (collection, href, etag, ics, dirty) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT (collection, href) DO UPDATE SET etag = excluded.etag, ics = excluded.ics, dirty = excluded.dirty"_s,
        {item.collectionId, item.href, item.etag, item.ics, item.dirty});
}

void Database::removeItem(const QString &collectionId, const QString &href)
{
    run(u"DELETE FROM items WHERE collection = ? AND href = ?"_s, {collectionId, href});
}
