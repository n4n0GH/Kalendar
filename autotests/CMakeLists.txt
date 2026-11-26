# SPDX-FileCopyrightText: 2026 Kalendar contributors
# SPDX-License-Identifier: BSD-2-Clause

ecm_add_test(davparsertest.cpp
    TEST_NAME davparsertest
    LINK_LIBRARIES kalendarcore Qt6::Test
)

ecm_add_test(enginetest.cpp
    TEST_NAME enginetest
    LINK_LIBRARIES kalendarcore Qt6::Test
)

# Synchronizes with a real CalDAV server (Radicale) that it starts itself;
# skipped where Radicale is not installed. KALENDAR_RADICALE names the Python
# to run it with.
ecm_add_test(synctest.cpp
    TEST_NAME synctest
    LINK_LIBRARIES kalendarcore Qt6::Test
)

# Google's Calendar API, with a stand-in for it (fakegoogle.py) that the test
# starts itself.
ecm_add_test(googletest.cpp
    TEST_NAME googletest
    LINK_LIBRARIES kalendarcore Qt6::Test
)

# The system's Online Accounts. KALENDAR_LIVE_GOOGLE=1 also has it read the
# calendars of the Google accounts the system is really signed in to.
ecm_add_test(onlineaccountstest.cpp
        ${CMAKE_SOURCE_DIR}/src/onlineaccounts.cpp
    TEST_NAME onlineaccountstest
    LINK_LIBRARIES kalendarcore Qt6::Test Qt6::DBus Qt6::Sql KF6::I18n
)
target_compile_definitions(onlineaccountstest PRIVATE TRANSLATION_DOMAIN="kalendar")
target_include_directories(onlineaccountstest PRIVATE ${CMAKE_SOURCE_DIR}/src)

# The calendar files that Akonadi shows, with a stand-in for Akonadi on the
# session bus; skipped where there is no session bus.
ecm_add_test(systemcalendarstest.cpp
        ${CMAKE_SOURCE_DIR}/src/systemcalendars.cpp
    TEST_NAME systemcalendarstest
    LINK_LIBRARIES kalendarcore Qt6::Test Qt6::DBus
)
target_include_directories(systemcalendarstest PRIVATE ${CMAKE_SOURCE_DIR}/src)

# Handing Kalendar's events to Akonadi as a calendar file, with the same
# kind of stand-in; and clearing away the plugin for Plasma's clock that
# earlier versions installed.
ecm_add_test(systemexporttest.cpp
        ${CMAKE_SOURCE_DIR}/src/systemcalendars.cpp
        ${CMAKE_SOURCE_DIR}/src/systemexport.cpp
    TEST_NAME systemexporttest
    LINK_LIBRARIES kalendarcore Qt6::Test Qt6::DBus Qt6::Qml KF6::I18n
)
target_compile_definitions(systemexporttest PRIVATE TRANSLATION_DOMAIN="kalendar")
target_include_directories(systemexporttest PRIVATE ${CMAKE_SOURCE_DIR}/src)

# The colors of the desktop in a Flatpak: the scheme file made of the portal's.
ecm_add_test(desktopcolorstest.cpp
        ${CMAKE_SOURCE_DIR}/src/desktopcolors.cpp
    TEST_NAME desktopcolorstest
    LINK_LIBRARIES Qt6::Test Qt6::DBus Qt6::Widgets
)
target_include_directories(desktopcolorstest PRIVATE ${CMAKE_SOURCE_DIR}/src)

# The layer between the interface and the calendars, with the same server.
ecm_add_test(backendtest.cpp
        ${CMAKE_SOURCE_DIR}/src/backend.cpp
        ${CMAKE_SOURCE_DIR}/src/onlineaccounts.cpp
        ${CMAKE_SOURCE_DIR}/src/secretstore.cpp
        ${CMAKE_SOURCE_DIR}/src/systemcalendars.cpp
    TEST_NAME backendtest
    LINK_LIBRARIES kalendarcore Qt6::Test Qt6::Qml Qt6::Gui Qt6::Network Qt6::DBus KF6::I18n KF6::Wallet
)
target_compile_definitions(backendtest PRIVATE TRANSLATION_DOMAIN="kalendar")
target_include_directories(backendtest PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/src)
