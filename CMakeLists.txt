# SPDX-FileCopyrightText: 2026 Kalendar contributors
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.20)

project(kalendar VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.8.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMAddTests)
include(ECMFindQmlModule)
include(ECMQmlModule)
include(ECMSetupVersion)
include(FeatureSummary)

ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX KALENDAR
    VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/kalendar-version.h
)

# Signing in to Google needs an OAuth client of type "Desktop app", which
# whoever distributes Kalendar registers at https://console.cloud.google.com
# (enable the "CalDAV API"). Without these, the user is asked for one.
set(KALENDAR_GOOGLE_CLIENT_ID "" CACHE STRING "OAuth client ID for Google accounts")
set(KALENDAR_GOOGLE_CLIENT_SECRET "" CACHE STRING "OAuth client secret for Google accounts")

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
    Sql
    Widgets
)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CalendarCore
    ColorScheme
    CoreAddons
    I18n
    Kirigami
    Wallet
)

# For showing the events in the calendar of Plasma's clock. The plugin belongs
# to the system's Plasma, so it is of no use inside a Flatpak.
option(BUILD_PLASMA_PLUGIN "Build the plugin for the calendar of Plasma's clock" ON)
if(BUILD_PLASMA_PLUGIN)
    find_package(KF6Declarative ${KF_MIN_VERSION})
endif()
set_package_properties(KF6Declarative PROPERTIES
    TYPE OPTIONAL
    PURPOSE "Shows Kalendar's events in the calendar of Plasma's clock"
)

# QML modules that are only needed at run time.
ecm_find_qmlmodule(org.kde.kirigami REQUIRED)
ecm_find_qmlmodule(org.kde.coreaddons REQUIRED)

add_subdirectory(src)

if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    add_subdirectory(autotests)
endif()

install(PROGRAMS io.github.n4n0gh.kalendar.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES io.github.n4n0gh.kalendar.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES icons/io.github.n4n0gh.kalendar.svg DESTINATION ${KDE_INSTALL_FULL_ICONDIR}/hicolor/scalable/apps)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/po)
    ki18n_install(po)
endif()

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
