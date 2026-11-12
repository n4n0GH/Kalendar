// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clockplugin.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace
{
const QString PluginName = u"plasmacalendarplugins/kalendarevents.so"_s;
// Relative to the user's folder.
const QString PluginRoot = u".local/lib/kalendar/plugins"_s;

/// The plugin that came with this Kalendar.
QString shipped()
{
    const QStringList places{
        // In a build folder, and where the app is installed.
        QCoreApplication::applicationDirPath() + u'/' + PluginName,
        QStringLiteral(KALENDAR_PLUGIN_DIR) + u'/' + PluginName,
    };
    for (const QString &place : places) {
        if (QFile::exists(place)) {
            return place;
        }
    }
    return {};
}

QByteArray contentsOf(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool write(const QString &path, const QByteArray &contents, QString *error)
{
    if (!QDir().mkpath(QFileInfo(path).path())) {
        *error = i18nc("@info %1 is a folder", "The folder “%1” could not be created.", QFileInfo(path).path());
        return false;
    }
    // Written beside it and then put in its place, so that a Plasma that
    // is using the old one is not disturbed.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        *error = i18nc("@info %1 is a file, %2 a technical reason", "“%1” could not be written: %2", path, file.errorString());
        return false;
    }
    return true;
}
}

ClockPlugin::ClockPlugin(const QString &home, const QString &source, QObject *parent)
    : QObject(parent)
    // Tests leave the user's own folders alone.
    , m_home(!home.isEmpty() ? home : QDir::homePath() + (qEnvironmentVariableIsSet("KALENDAR_TEST_MODE") ? u"/.qttest"_s : QString()))
    , m_source(source.isEmpty() ? shipped() : source)
{
}

QString ClockPlugin::pluginFile() const
{
    return m_home + u'/' + PluginRoot + u'/' + PluginName;
}

QString ClockPlugin::loginScript() const
{
    // Inside a Flatpak the settings folder is the app's own; the system's is named here.
    QString config = qEnvironmentVariable("HOST_XDG_CONFIG_HOME");
    if (config.isEmpty() || m_home != QDir::homePath()) {
        config = m_home + u"/.config"_s;
    }
    return config + u"/plasma-workspace/env/kalendar-clock-plugin.sh"_s;
}

bool ClockPlugin::isAvailable() const
{
    return !m_source.isEmpty();
}

bool ClockPlugin::isSystemWide() const
{
    // A Flatpak's own folders are not the system's.
    if (m_source.isEmpty() || QFile::exists(u"/.flatpak-info"_s)) {
        return false;
    }
    const QString folder = QFileInfo(QFileInfo(m_source).path()).canonicalPath();
    for (const QString &path : QCoreApplication::libraryPaths()) {
        if (QFileInfo(path).canonicalFilePath() == folder && !folder.startsWith(QDir::homePath())) {
            return true;
        }
    }
    return false;
}

bool ClockPlugin::isInstalled() const
{
    return QFile::exists(pluginFile()) && QFile::exists(loginScript());
}

bool ClockPlugin::isFresh() const
{
    return m_fresh && isInstalled();
}

QString ClockPlugin::install()
{
    if (!isAvailable()) {
        return i18nc("@info", "This Kalendar comes without the plugin.");
    }
    const QByteArray plugin = contentsOf(m_source);
    if (plugin.isEmpty()) {
        return i18nc("@info %1 is a file", "“%1” could not be read.", m_source);
    }
    QString error;
    // Plasma runs what is in this folder when it starts.
    const QByteArray script = "# Written by Kalendar: lets Plasma find the plugin that shows Kalendar's\n"
                              "# events in the calendar of the clock. Kalendar's settings remove it again.\n"
                              "export QT_PLUGIN_PATH=\"$HOME/"
        + PluginRoot.toUtf8() + "${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}\"\n";
    if (write(pluginFile(), plugin, &error) && write(loginScript(), script, &error)) {
        m_fresh = true;
    }
    Q_EMIT installedChanged();
    return error;
}

QString ClockPlugin::remove()
{
    QString error;
    for (const QString &path : {pluginFile(), loginScript()}) {
        QFile file(path);
        if (file.exists() && !file.remove()) {
            error = i18nc("@info %1 is a file, %2 a technical reason", "“%1” could not be removed: %2", path, file.errorString());
        }
    }
    // The folders made for it, as far as nothing else is in them.
    QDir().rmpath(QFileInfo(pluginFile()).path());
    m_fresh = false;
    Q_EMIT installedChanged();
    return error;
}

void ClockPlugin::refresh()
{
    if (!isAvailable() || !isInstalled()) {
        return;
    }
    const auto hash = [](const QString &path) {
        return QCryptographicHash::hash(contentsOf(path), QCryptographicHash::Sha1);
    };
    if (hash(m_source) != hash(pluginFile())) {
        QString error;
        write(pluginFile(), contentsOf(m_source), &error);
    }
}
