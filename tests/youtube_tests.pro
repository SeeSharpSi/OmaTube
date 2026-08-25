QT += testlib

TEMPLATE = app
TARGET = youtube_tests
CONFIG += testcase
CONFIG -= app_bundle

PROJECT_ROOT = $$clean_path($$PWD/..)
include($$PROJECT_ROOT/ytclient_core.pri)

SOURCES += $$PROJECT_ROOT/tests/youtube_test.cpp
