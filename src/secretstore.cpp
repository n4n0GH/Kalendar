// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "secretstore.h"

#include <KLocalizedString>
#include <KWallet>

#include <QGuiApplication>
#include <QWindow>

using namespace Qt::StringLiterals;

namespace
{
const QString Folder = u"Kalendar"_s;
}

SecretStore::SecretStore(bool inMemory, QObject *parent)
    : QObject(parent)
    , m_inMemory(inMemory)
{
}

SecretStore::~SecretStore() = default;

void SecretStore::whenOpen(const std::function<void(const QString &error)> &task)
{
    if (m_inMemory || m_ready) {
        task({});
        return;
    }
    m_waiting.append(task);
    if (m_wallet) {
        return; // being opened
    }
    if (!KWallet::Wallet::isEnabled()) {
        opened(false);
        return;
    }
    // The wallet's own dialogs belong to the window that asked.
    const QWindowList windows = QGuiApplication::topLevelWindows();
    const WId window = windows.isEmpty() ? 0 : windows.first()->winId();
    m_wallet.reset(KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), window, KWallet::Wallet::Asynchronous));
    if (!m_wallet) {
        opened(false);
        return;
    }
    connect(m_wallet.get(), &KWallet::Wallet::walletOpened, this, &SecretStore::opened);
    connect(m_wallet.get(), &KWallet::Wallet::walletClosed, this, [this] {
        // Closed from outside: opened again when next needed.
        m_wallet.release()->deleteLater();
        m_ready = false;
    });
}

void SecretStore::opened(bool success)
{
    m_ready = success && m_wallet && (m_wallet->hasFolder(Folder) || m_wallet->createFolder(Folder)) && m_wallet->setFolder(Folder);
    QString error;
    if (!m_ready) {
        if (m_wallet) {
            m_wallet.release()->deleteLater();
        }
        error = KWallet::Wallet::isEnabled()
            ? i18nc("@info", "The wallet was not opened, so the account's password is not available.")
            : i18nc("@info", "The wallet system (KWallet) is switched off, so passwords cannot be kept safely. Switch it on in System Settings.");
    }
    const auto waiting = m_waiting;
    m_waiting.clear();
    for (const auto &task : waiting) {
        task(error);
    }
}

void SecretStore::read(const QString &key, const std::function<void(const QString &secret, const QString &error)> &done)
{
    whenOpen([=, this](const QString &error) {
        QString secret;
        if (m_inMemory) {
            secret = m_memory.value(key);
        } else if (error.isEmpty()) {
            m_wallet->readPassword(key, secret);
        }
        done(secret, error);
    });
}

void SecretStore::write(const QString &key, const QString &secret, const std::function<void(const QString &error)> &done)
{
    whenOpen([=, this](const QString &error) {
        if (m_inMemory) {
            m_memory.insert(key, secret);
            done({});
        } else if (!error.isEmpty()) {
            done(error);
        } else if (m_wallet->writePassword(key, secret) != 0) {
            done(i18nc("@info", "The password could not be saved in the wallet."));
        } else {
            done({});
        }
    });
}

void SecretStore::remove(const QString &key)
{
    whenOpen([=, this](const QString &error) {
        if (m_inMemory) {
            m_memory.remove(key);
        } else if (error.isEmpty()) {
            m_wallet->removeEntry(key);
        }
    });
}
