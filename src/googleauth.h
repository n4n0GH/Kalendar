// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointer>

#include <functional>

class QNetworkAccessManager;
class QTcpServer;
class QTimer;

/// Signs in to a Google account the way Google asks desktop apps to
/// (OAuth 2.0 with PKCE, RFC 8252): the system's browser shows Google's
/// sign-in page, which hands the result to a local port Kalendar listens
/// on. Kalendar never sees the password.
class GoogleAuth : public QObject
{
    Q_OBJECT

public:
    struct Result {
        /// Empty on success.
        QString error;
        QString accessToken;
        /// How long the access token is good for, in seconds.
        int expiresIn = 0;
        /// Lets new access tokens be fetched later. Only after signIn().
        QString refreshToken;
        /// The account's address. Only after signIn().
        QString email;
    };
    using Callback = std::function<void(const Result &)>;

    explicit GoogleAuth(QNetworkAccessManager *network, QObject *parent = nullptr);

    /// Opens the browser and waits for the user to sign in there.
    void signIn(const QString &clientId, const QString &clientSecret, const Callback &done);
    /// Gives up waiting; the callback of signIn() is not called.
    void cancel();
    void refresh(const QString &clientId, const QString &clientSecret, const QString &refreshToken, const Callback &done);

    /// Where the calendars of the account @p email are.
    static QUrl calDavAddress(const QString &email);

private:
    void requestTokens(const QList<QPair<QString, QString>> &form, const Callback &done);

    QNetworkAccessManager *const m_network;
    QPointer<QTcpServer> m_server;
    QPointer<QTimer> m_timeout;
};
