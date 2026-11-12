// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QQmlEngine>

/// Puts the plugin that shows Kalendar's events in the calendar of Plasma's
/// clock where Plasma finds it, for this user only and without asking for an
/// administrator.
///
/// Plasma only looks for plugins where the system has them, so two things
/// are needed: a copy of the plugin in the user's folder, and a line that
/// tells Plasma at login to look there too.
class ClockPlugin : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Whether this Kalendar comes with the plugin at all.
    Q_PROPERTY(bool available READ isAvailable CONSTANT)
    /// Whether the plugin came with the system's packages, which leaves
    /// nothing to install or remove here.
    Q_PROPERTY(bool systemWide READ isSystemWide CONSTANT)
    Q_PROPERTY(bool installed READ isInstalled NOTIFY installedChanged)
    /// Whether it was installed since Kalendar started, so that Plasma does
    /// not know of it yet.
    Q_PROPERTY(bool fresh READ isFresh NOTIFY installedChanged)

public:
    /// @p home is the user's folder and @p source the plugin to install,
    /// unless a test says otherwise.
    explicit ClockPlugin(const QString &home = {}, const QString &source = {}, QObject *parent = nullptr);

    bool isAvailable() const;
    bool isSystemWide() const;
    bool isInstalled() const;
    bool isFresh() const;

    /// Both give an error message, or an empty string.
    Q_INVOKABLE QString install();
    Q_INVOKABLE QString remove();
    /// Replaces an installed copy that is of another version of Kalendar.
    void refresh();

    QString pluginFile() const;
    QString loginScript() const;

Q_SIGNALS:
    void installedChanged();

private:
    const QString m_home;
    const QString m_source;
    bool m_fresh = false;
};
