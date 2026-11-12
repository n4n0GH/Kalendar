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

# The layer between the interface and the calendars, with the same server.
ecm_add_test(backendtest.cpp
        ${CMAKE_SOURCE_DIR}/src/backend.cpp
        ${CMAKE_SOURCE_DIR}/src/googleauth.cpp
        ${CMAKE_SOURCE_DIR}/src/secretstore.cpp
    TEST_NAME backendtest
    LINK_LIBRARIES kalendarcore Qt6::Test Qt6::Qml Qt6::Gui Qt6::Network KF6::I18n KF6::Wallet
)
target_compile_definitions(backendtest PRIVATE TRANSLATION_DOMAIN="kalendar")
target_include_directories(backendtest PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR}/src)

if(TARGET kalendarevents)
    ecm_add_test(plasmaplugintest.cpp
        TEST_NAME plasmaplugintest
        LINK_LIBRARIES kalendarcore KF6::CalendarEvents Qt6::Test
    )
    target_compile_definitions(plasmaplugintest PRIVATE PLUGIN_FILE="$<TARGET_FILE:kalendarevents>")
    add_dependencies(plasmaplugintest kalendarevents)
endif()
