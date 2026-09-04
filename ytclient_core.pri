QT += core gui network sql

CONFIG += c++20 release strict_c++
CONFIG -= c++11 c++14 c++17 debug debug_and_release

!versionAtLeast(QT_VERSION, 6.8.0) {
    error("YT Client requires Qt 6.8 or newer")
}

greaterThan(QMAKE_GCC_MAJOR_VERSION, 15) {
    QMAKE_CXXFLAGS += -Wno-sfinae-incomplete
}

INCLUDEPATH += $$PROJECT_ROOT/src

HEADERS += \
    $$PROJECT_ROOT/src/domain.h \
    $$PROJECT_ROOT/src/models/categorymodel.h \
    $$PROJECT_ROOT/src/models/channelmodel.h \
    $$PROJECT_ROOT/src/models/feedmodel.h \
    $$PROJECT_ROOT/src/models/historymodel.h \
    $$PROJECT_ROOT/src/models/livechannelmodel.h \
    $$PROJECT_ROOT/src/models/watchnextmodel.h \
    $$PROJECT_ROOT/src/playbacksettings.h \
    $$PROJECT_ROOT/src/pointerwatch.h \
    $$PROJECT_ROOT/src/refreshservice.h \
    $$PROJECT_ROOT/src/repository.h \
    $$PROJECT_ROOT/src/watchtracker.h \
    $$PROJECT_ROOT/src/youtubeclient.h \
    $$PROJECT_ROOT/src/youtubefeed.h

SOURCES += \
    $$PROJECT_ROOT/src/models/categorymodel.cpp \
    $$PROJECT_ROOT/src/models/channelmodel.cpp \
    $$PROJECT_ROOT/src/models/feedmodel.cpp \
    $$PROJECT_ROOT/src/models/historymodel.cpp \
    $$PROJECT_ROOT/src/models/livechannelmodel.cpp \
    $$PROJECT_ROOT/src/models/watchnextmodel.cpp \
    $$PROJECT_ROOT/src/playbacksettings.cpp \
    $$PROJECT_ROOT/src/pointerwatch.cpp \
    $$PROJECT_ROOT/src/refreshservice.cpp \
    $$PROJECT_ROOT/src/repository.cpp \
    $$PROJECT_ROOT/src/watchtracker.cpp \
    $$PROJECT_ROOT/src/youtubeclient.cpp \
    $$PROJECT_ROOT/src/youtubefeed.cpp
