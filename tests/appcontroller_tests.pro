QT += testlib qml

TEMPLATE = app
TARGET = appcontroller_tests
CONFIG += testcase
CONFIG -= app_bundle

PROJECT_ROOT = $$clean_path($$PWD/..)
include($$PROJECT_ROOT/ytclient_core.pri)

HEADERS += \
    $$PROJECT_ROOT/src/appcontroller.h \
    $$PROJECT_ROOT/src/thememanager.h
SOURCES += \
    $$PROJECT_ROOT/src/appcontroller.cpp \
    $$PROJECT_ROOT/src/thememanager.cpp \
    $$PROJECT_ROOT/tests/appcontroller_test.cpp
RESOURCES += $$PROJECT_ROOT/qml/resources.qrc
