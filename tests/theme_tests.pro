QT += core gui testlib

TEMPLATE = app
TARGET = theme_tests
CONFIG += c++20 testcase strict_c++
CONFIG -= app_bundle c++11 c++14 c++17 debug_and_release

greaterThan(QMAKE_GCC_MAJOR_VERSION, 15) {
    QMAKE_CXXFLAGS += -Wno-sfinae-incomplete
}

PROJECT_ROOT = $$PWD/..
INCLUDEPATH += $$PROJECT_ROOT/src

HEADERS += $$PROJECT_ROOT/src/thememanager.h
SOURCES += \
    $$PROJECT_ROOT/src/thememanager.cpp \
    $$PROJECT_ROOT/tests/theme_test.cpp
RESOURCES += $$PROJECT_ROOT/qml/resources.qrc
