QT += testlib

TEMPLATE = app
TARGET = repository_tests
CONFIG += testcase
CONFIG -= app_bundle

PROJECT_ROOT = $$clean_path($$PWD/..)
include($$PROJECT_ROOT/ytclient_core.pri)

SOURCES += $$PROJECT_ROOT/tests/repository_test.cpp
