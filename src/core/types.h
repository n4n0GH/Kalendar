// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QString>
#include <QUrl>

/// Where calendars come from: this system, or a server.
struct Account {
    enum Type {
        Local = 0,
        CalDav = 1,
        /// A Google account that the system's Online Accounts signed in to.
        Google = 2,
    };

    QString id;
    Type type = Local;
    /// Shown to the user, such as "ada@example.org".
    QString name;
    /// The folder on the server that holds the calendars (the "calendar
    /// home"); for Google, where its Calendar API is.
    QUrl url;
    /// Who signs in; for Google, the number of the account in Online Accounts.
    QString user;
};

/// One calendar of an account.
struct Collection {
    QString id;
    QString accountId;
    QUrl url;
    QString name;
    QColor color;
    /// The server's marker for "something in here changed"; empty when unknown.
    QString ctag;
    /// Whether its events are shown.
    bool enabled = true;
    bool readOnly = false;
};

/// One stored calendar file (an event, with the exceptions to its repetition).
struct ItemRecord {
    enum Dirty {
        Clean = 0,
        /// Changed here and not yet sent to the server.
        Changed = 1,
        /// Deleted here and not yet deleted on the server.
        Deleted = 2,
    };

    QString collectionId;
    QString href;
    QString etag;
    QByteArray ics;
    int dirty = Clean;
};

/// A calendar as a server describes it.
struct CalendarInfo {
    QUrl url;
    QString name;
    QColor color;
    QString ctag;
    bool readOnly = false;
};
