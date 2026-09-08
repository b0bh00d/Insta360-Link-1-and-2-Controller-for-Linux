# insta360link.pro - top-level subdirs project.
#
#   app  -> insta360linkgui  (Qt Widgets GUI)
#   cli  -> linkctl          (QtCore console tool)
#
# Both pull in the shared V4L2 / Insta360 Link controller code via
# src/core/core.pri.
#
# Build:
#   qmake && make -j$(nproc)
#
# The .pro files are Qt5/Qt6 agnostic; `qmake` picks whichever Qt is on PATH.

TEMPLATE = subdirs

SUBDIRS = \
    app \
    cli
