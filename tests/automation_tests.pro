QT += testlib qml quick quickcontrols2

TEMPLATE = app
TARGET = automation_tests
CONFIG += c++20 testcase
CONFIG -= app_bundle

PROJECT_ROOT = $$clean_path($$PWD/..)
INCLUDEPATH += $$PROJECT_ROOT/src

HEADERS += \
    $$PROJECT_ROOT/src/automationrunner.h \
    $$PROJECT_ROOT/src/spaceholdhandler.h
SOURCES += \
    $$PROJECT_ROOT/src/automationrunner.cpp \
    $$PROJECT_ROOT/src/spaceholdhandler.cpp \
    $$PROJECT_ROOT/tests/automation_test.cpp
