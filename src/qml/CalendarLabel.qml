// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A calendar in a list to choose from: its color, its name, and where it
// is kept.
QQC2.Control {
    id: label

    property string name
    property string source
    property color calendarColor
    // On the color of a chosen row, where a dot of that color would vanish.
    property bool highlighted: false

    padding: 0

    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            implicitWidth: Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
            implicitHeight: implicitWidth
            radius: height / 2
            color: label.calendarColor
            border.width: label.highlighted ? 1 : 0
            border.color: Kirigami.Theme.highlightedTextColor
        }
        QQC2.Label {
            text: label.name
            elide: Text.ElideRight
            textFormat: Text.PlainText
            Layout.maximumWidth: Math.max(Kirigami.Units.gridUnit * 4, label.availableWidth * 0.6)
        }
        QQC2.Label {
            Layout.fillWidth: true
            text: label.source
            elide: Text.ElideRight
            textFormat: Text.PlainText
            opacity: 0.75
        }
    }
}
