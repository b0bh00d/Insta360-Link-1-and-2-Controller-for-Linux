# app.pro - Qt Widgets GUI (insta360linkgui).

TEMPLATE = app
TARGET   = insta360linkgui

QT += core gui widgets
CONFIG += c++17
CONFIG -= app_bundle

# Shared V4L2 / Insta360 Link controller code.
include(../src/core/core.pri)

HEADERS += \
    mainwindow.h \
    videocapture.h

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    videocapture.cpp

target.path = /usr/local/bin
INSTALLS += target
