// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariant>

#include <functional>

/// The accounts set up under "Online Accounts" in System Settings. Signing
/// in happens there, once for all apps; here they are only listed and asked
/// for the token that lets Kalendar act for the account.
///
/// Two things of the system are used directly rather than through their
/// libraries, which the runtime of the Flatpak does not have: the list of
/// accounts (a database of libaccounts) and the service that holds the
/// sign-ins (signond, over D-Bus).
class OnlineAccounts : public QObject
{
    Q_OBJECT

public:
    struct Entry {
        quint32 id = 0;
        QString name;
    };
    /// @p error is empty when there is a token.
    using TokenCallback = std::function<void(const QString &token, int expiresIn, const QString &error)>;

    /// With @p testMode there is no system to ask: the accounts are those
    /// named in KALENDAR_ONLINE_ACCOUNTS ("1=Work,2=Home").
    explicit OnlineAccounts(bool testMode, QObject *parent = nullptr);

    /// The Google accounts that are set up and switched on.
    QList<Entry> googleAccounts() const;
    /// Asks for a token for the account @p id. Never shows a window: when
    /// Google wants the user to sign in again, that is reported as an error.
    void token(quint32 id, const TokenCallback &done);

    /// Reads a value as libaccounts writes it down, such as 'text', true,
    /// 7 or ['a', 'b'], given its type (s, b, u, as, …).
    static QVariant parseValue(const QString &type, const QString &text);

private:
    QString databasePath() const;
    /// What the sign-in service needs to know to renew a token.
    QVariantMap signInSettings(quint32 id, quint32 *credentials, QString *method, QString *mechanism) const;

    const bool m_testMode;
};
