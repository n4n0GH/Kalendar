// SPDX-FileCopyrightText: 2026 Kalendar contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>

// Answers of CalDAV servers. They are in a file of their own because the
// tool that reads the test (moc) stumbles over them.
namespace Samples
{
// As sent by a Nextcloud server, shortened.
inline const QByteArray calendarList = R"(<?xml version="1.0"?>
<d:multistatus xmlns:d="DAV:" xmlns:cal="urn:ietf:params:xml:ns:caldav" xmlns:cs="http://calendarserver.org/ns/" xmlns:x1="http://apple.com/ns/ical/">
 <d:response>
  <d:href>/remote.php/dav/calendars/ada/</d:href>
  <d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat>
  <d:propstat><d:prop><d:displayname/><cs:getctag/></d:prop><d:status>HTTP/1.1 404 Not Found</d:status></d:propstat>
 </d:response>
 <d:response>
  <d:href>/remote.php/dav/calendars/ada/work%20stuff/</d:href>
  <d:propstat>
   <d:prop>
    <d:resourcetype><d:collection/><cal:calendar/></d:resourcetype>
    <d:displayname>Work &amp; Stuff</d:displayname>
    <x1:calendar-color>#F64F00FF</x1:calendar-color>
    <cs:getctag>http://sabre.io/ns/sync/42</cs:getctag>
    <cal:supported-calendar-component-set><cal:comp name="VEVENT"/><cal:comp name="VTODO"/></cal:supported-calendar-component-set>
    <d:current-user-privilege-set><d:privilege><d:read/></d:privilege><d:privilege><d:write/></d:privilege></d:current-user-privilege-set>
   </d:prop>
   <d:status>HTTP/1.1 200 OK</d:status>
  </d:propstat>
 </d:response>
</d:multistatus>)";

inline const QByteArray principalAndData = R"(<multistatus xmlns="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav">
 <response>
  <href>/</href>
  <propstat><prop>
   <current-user-principal><href>/principals/ada/</href></current-user-principal>
   <C:calendar-home-set><href>https://other.example.org/cal/ada/</href></C:calendar-home-set>
  </prop><status>HTTP/1.1 200 OK</status></propstat>
 </response>
 <response>
  <href>/cal/ada/work/1.ics</href>
  <propstat><prop>
   <getetag>"abc"</getetag>
   <C:calendar-data><![CDATA[BEGIN:VCALENDAR
END:VCALENDAR
]]></C:calendar-data>
  </prop><status>HTTP/1.1 200 OK</status></propstat>
 </response>
 <response><href>/cal/ada/work/gone.ics</href><status>HTTP/1.1 404 Not Found</status></response>
</multistatus>)";
}
