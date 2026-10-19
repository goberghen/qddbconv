QT += core gui widgets concurrent
CONFIG += c++17
TARGET = qddbconv
VERSION = 1.3.1
TEMPLATE = app

INCLUDEPATH += src

HEADERS += \
    src/preset.h \
    src/track.h \
    src/yamaha_ma_tables.h \
    src/yamaha_ma_fm.h \
    src/smaf2mid.h \
    src/converter.h \
    src/mainwindow.h

SOURCES += \
    src/main.cpp \
    src/preset.cpp \
    src/track.cpp \
    src/yamaha_ma_fm.cpp \
    src/smaf2mid.cpp \
    src/converter.cpp \
    src/mainwindow.cpp

win32: RC_FILE = qddbconv.rc
