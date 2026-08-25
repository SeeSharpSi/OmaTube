QT += qml quick quickcontrols2

TEMPLATE = app
TARGET = yt-client
VERSION = 0.1.0

PROJECT_ROOT = $$PWD
include($$PROJECT_ROOT/ytclient_core.pri)

HEADERS += \
    $$PROJECT_ROOT/src/appcontroller.h \
    $$PROJECT_ROOT/src/thememanager.h
SOURCES += \
    $$PROJECT_ROOT/src/appcontroller.cpp \
    $$PROJECT_ROOT/src/main.cpp \
    $$PROJECT_ROOT/src/thememanager.cpp
RESOURCES += $$PROJECT_ROOT/qml/resources.qrc

macx {
    QMAKE_INFO_PLIST = $$PROJECT_ROOT/macos/Info.plist
}
