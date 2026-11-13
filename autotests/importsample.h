// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>

/// A calendar file as another program writes it: an event that repeats in
/// another time zone with one exception, an event of whole days whose UID
/// makes no file name, and a to-do.
inline const QByteArray ImportSample =
    "BEGIN:VCALENDAR\r\n"
    "VERSION:2.0\r\n"
    "PRODID:-//Elsewhere//Calendar//EN\r\n"
    "BEGIN:VTIMEZONE\r\n"
    "TZID:America/New_York\r\n"
    "BEGIN:STANDARD\r\n"
    "DTSTART:20071104T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU\r\n"
    "TZOFFSETFROM:-0400\r\n"
    "TZOFFSETTO:-0500\r\n"
    "END:STANDARD\r\n"
    "BEGIN:DAYLIGHT\r\n"
    "DTSTART:20070311T020000\r\n"
    "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=2SU\r\n"
    "TZOFFSETFROM:-0500\r\n"
    "TZOFFSETTO:-0400\r\n"
    "END:DAYLIGHT\r\n"
    "END:VTIMEZONE\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:call@elsewhere.example\r\n"
    "DTSTAMP:20261101T000000Z\r\n"
    "SUMMARY:Call with New York\r\n"
    "DTSTART;TZID=America/New_York:20261116T100000\r\n"
    "DTEND;TZID=America/New_York:20261116T103000\r\n"
    "RRULE:FREQ=WEEKLY;COUNT=3\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:call@elsewhere.example\r\n"
    "DTSTAMP:20261101T000000Z\r\n"
    "RECURRENCE-ID;TZID=America/New_York:20261123T100000\r\n"
    "SUMMARY:Call with New York (late)\r\n"
    "DTSTART;TZID=America/New_York:20261123T120000\r\n"
    "DTEND;TZID=America/New_York:20261123T123000\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VEVENT\r\n"
    "UID:a name/with spaces & more\r\n"
    "DTSTAMP:20261101T000000Z\r\n"
    "SUMMARY:Fair\r\n"
    "LOCATION:Hall 3\r\n"
    "DTSTART;VALUE=DATE:20261105\r\n"
    "DTEND;VALUE=DATE:20261107\r\n"
    "END:VEVENT\r\n"
    "BEGIN:VTODO\r\n"
    "UID:todo@elsewhere.example\r\n"
    "DTSTAMP:20261101T000000Z\r\n"
    "SUMMARY:Buy tickets\r\n"
    "END:VTODO\r\n"
    "END:VCALENDAR\r\n";
