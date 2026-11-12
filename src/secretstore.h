// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>

#include <functional>
#include <memory>

namespace KWallet
{
class Wallet;
}

/// Keeps passwords and sign-in tokens in the user's wallet (KWallet) rather
/// than in a file.
///
/// The wallet is only opened when something is asked of it, as that may
/// prompt for the wallet's password.
class SecretStore : public QObject
{
    Q_OBJECT

public:
    /// With @p inMemory, nothing is kept beyond this run and the wallet is
    /// left alone: for tests.
    explicit SecretStore(bool inMemory = false, QObject *parent = nullptr);
    ~SecretStore() override;

    /// Gives the secret stored under @p key (empty if none), or why the
    /// wallet cannot be used.
    void read(const QString &key, const std::function<void(const QString &secret, const QString &error)> &done);
    /// Gives an error message, or an empty string.
    void write(const QString &key, const QString &secret, const std::function<void(const QString &error)> &done);
    void remove(const QString &key);

private:
    void whenOpen(const std::function<void(const QString &error)> &task);
    void opened(bool success);

    const bool m_inMemory;
    QHash<QString, QString> m_memory;
    std::unique_ptr<KWallet::Wallet> m_wallet;
    bool m_ready = false;
    QList<std::function<void(const QString &error)>> m_waiting;
};
