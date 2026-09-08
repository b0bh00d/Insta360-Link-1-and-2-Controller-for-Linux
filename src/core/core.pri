# core.pri - shared V4L2 / Insta360 Link controller sources.
# Included by both app/app.pro and cli/cli.pro so the core compiles
# straight into each target (no separate library to link).

INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/v4l2.h \
    $$PWD/insta360link.h

SOURCES += \
    $$PWD/v4l2.cpp \
    $$PWD/insta360link.cpp
