QT += core gui widgets concurrent
CONFIG += c++17
TARGET = qddbconv
VERSION = 1.2.4
TEMPLATE = app

INCLUDEPATH += src

HEADERS += \
    src/preset.h \
    src/track.h \
    src/smaf2mid.h \
    src/converter.h \
    src/mainwindow.h

SOURCES += \
    src/main.cpp \
    src/preset.cpp \
    src/track.cpp \
    src/smaf2mid.cpp \
    src/converter.cpp \
    src/mainwindow.cpp

win32: RC_FILE = qddbconv.rc
