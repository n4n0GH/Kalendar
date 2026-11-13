# SPDX-FileCopyrightText: 2026 Kalendar contributors
# SPDX-License-Identifier: BSD-2-Clause

# Storage and synchronization: no GUI, so that it can be tested on its own.
add_library(kalendarcore STATIC
    core/database.cpp
    core/database.h
    core/davclient.cpp
    core/davclient.h
    core/engine.cpp
    core/engine.h
    core/googleclient.cpp
    core/googleclient.h
    core/types.h
)
# It also goes into a plugin.
set_target_properties(kalendarcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(kalendarcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_compile_definitions(kalendarcore PRIVATE TRANSLATION_DOMAIN="kalendar")
target_link_libraries(kalendarcore
    PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql KF6::CalendarCore
    PRIVATE KF6::I18n
)

add_executable(kalendar
    main.cpp
    backend.cpp
    backend.h
    clockplugin.cpp
    clockplugin.h
    onlineaccounts.cpp
    onlineaccounts.h
    secretstore.cpp
    secretstore.h
    singleinstance.cpp
    singleinstance.h
)

ecm_add_qml_module(kalendar
    URI io.github.n4n0gh.kalendar
    GENERATE_PLUGIN_SOURCE
    DEPENDENCIES
        QtQuick
)

ecm_target_qml_sources(kalendar SOURCES
    qml/Main.qml
    qml/Sidebar.qml
    qml/CalendarPage.qml
    qml/MonthView.qml
    qml/WeekView.qml
    qml/AgendaView.qml
    qml/EventChip.qml
    qml/CalendarLabel.qml
    qml/CalendarBox.qml
    qml/MiniMonth.qml
    qml/DateButton.qml
    qml/TimeField.qml
    qml/EventDialog.qml
    qml/EventViewDialog.qml
    qml/ImportDialog.qml
    qml/SettingsPage.qml
    qml/AddAccountDialog.qml
)

target_include_directories(kalendar PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(kalendar PRIVATE TRANSLATION_DOMAIN="kalendar" KALENDAR_PLUGIN_DIR="${KDE_INSTALL_FULL_PLUGINDIR}")
target_link_libraries(kalendar PRIVATE
    kalendarcore
    Qt6::Core
    Qt6::DBus
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    KF6::ColorScheme
    KF6::CoreAddons
    KF6::I18n
    KF6::I18nQml
    KF6::Wallet
)

install(TARGETS kalendar ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# Shows the events in the calendar of Plasma's clock, where it is switched
# on in the clock's settings under "Calendar".
if(BUILD_PLASMA_PLUGIN AND TARGET KF6::CalendarEvents)
    add_library(kalendarevents MODULE
        plasma/kalendareventsplugin.cpp
        plasma/kalendareventsplugin.h
    )
    target_link_libraries(kalendarevents PRIVATE kalendarcore KF6::CalendarEvents)
    set_target_properties(kalendarevents PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plasmacalendarplugins)
    install(TARGETS kalendarevents DESTINATION ${KDE_INSTALL_PLUGINDIR}/plasmacalendarplugins COMPONENT plasmaplugin)
endif()
