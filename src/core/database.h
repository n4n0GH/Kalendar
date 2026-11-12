// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "types.h"

#include <QSqlDatabase>

/// Keeps accounts, calendars and their events on disk, so that the calendar
/// is there at once at start and without a network.
class Database
{
public:
    Database();
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /// With @p readOnly, the file must exist and nothing in it is touched.
    bool open(const QString &path, bool readOnly = false);
    QString errorString() const;

    QList<Account> accounts() const;
    void saveAccount(const Account &account);
    /// Also removes the account's calendars and events.
    void removeAccount(const QString &id);

    QList<Collection> collections() const;
    void saveCollection(const Collection &collection);
    /// Also removes the calendar's events.
    void removeCollection(const QString &id);

    /// Between these two, changes are written down together, which takes a
    /// fraction of the time that writing each down by itself does.
    void begin();
    void commit();

    QList<ItemRecord> items(const QString &collectionId) const;
    void saveItem(const ItemRecord &item);
    void removeItem(const QString &collectionId, const QString &href);

private:
    bool run(const QString &sql, const QVariantList &values = {}) const;

    QString m_connection;
    QSqlDatabase m_db;
    mutable QString m_error;
};
