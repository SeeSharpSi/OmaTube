QT += qml quick quickcontrols2

linux {
    QT += webenginequick
}

TEMPLATE = app
TARGET = yt-client
VERSION = 0.1.0

PROJECT_ROOT = $$PWD
include($$PROJECT_ROOT/ytclient_core.pri)

HEADERS += \
    $$PROJECT_ROOT/src/appcontroller.h \
    $$PROJECT_ROOT/src/automationfixture.h \
    $$PROJECT_ROOT/src/spaceholdhandler.h \
    $$PROJECT_ROOT/src/thememanager.h
SOURCES += \
    $$PROJECT_ROOT/src/appcontroller.cpp \
    $$PROJECT_ROOT/src/automationfixture.cpp \
    $$PROJECT_ROOT/src/main.cpp \
    $$PROJECT_ROOT/src/spaceholdhandler.cpp \
    $$PROJECT_ROOT/src/thememanager.cpp
RESOURCES += $$PROJECT_ROOT/qml/resources.qrc

packagesExist(mpv) {
    QT += opengl
    CONFIG += link_pkgconfig
    PKGCONFIG += mpv
    DEFINES += OMA_HAS_MPV
    HEADERS += $$PROJECT_ROOT/src/mpvplayer.h
    SOURCES += $$PROJECT_ROOT/src/mpvplayer.cpp
}

macx {
    QMAKE_INFO_PLIST = $$PROJECT_ROOT/macos/Info.plist
    HEADERS += $$PROJECT_ROOT/src/macvideoplayer.h
    OBJECTIVE_SOURCES += $$PROJECT_ROOT/src/macvideoplayer.mm
    QMAKE_OBJECTIVE_CFLAGS += -fno-objc-arc
    LIBS += -framework WebKit -framework AppKit
}
