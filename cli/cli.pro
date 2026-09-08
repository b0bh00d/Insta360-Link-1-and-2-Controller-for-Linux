# cli.pro - command-line controller (linkctl).

TEMPLATE = app
TARGET   = linkctl

QT = core
CONFIG += console c++17
CONFIG -= app_bundle

# Shared V4L2 / Insta360 Link controller code.
include(../src/core/core.pri)

SOURCES += main.cpp

target.path = /usr/local/bin
INSTALLS += target
