# Standalone probe for TTFFmpegWrapper::detectAudioBurst.
# Deliberately NOT wired into ttcut-ng.pro: it recompiles ttffmpegwrapper.cpp,
# which would slow every application build for a tool used during verification.
# Build manually:  qmake && make

# core+gui: ttffmpegwrapper.h includes <QImage> (QtGui) at the header level, so
# QtGui is needed to compile it even though no QtGui *symbols* are linked. The
# nm -uC symbol closure below misses this because it only sees linked symbols,
# not header include requirements. QtWidgets stays out.
QT       = core gui
CONFIG  += console c++17
CONFIG  -= app_bundle
TEMPLATE = app
TARGET   = ttcut-burst-probe

DESTDIR     = $$PWD
OBJECTS_DIR = $$PWD/obj
MOC_DIR     = $$PWD/moc

INCLUDEPATH += $$PWD/../..

# Link closure determined with: nm -uC obj/ttffmpegwrapper.o
# No TTCut and no TTESSmartCut symbols are referenced, so no QtGui/QtWidgets.
SOURCES += \
    main.cpp \
    ../../extern/ttffmpegwrapper.cpp \
    ../../avstream/ttdisplayordermap.cpp \
    ../../avstream/ttesinfo.cpp \
    ../../avstream/ttnaluparser.cpp \
    ../../common/ttmessagelogger.cpp \
    ../../common/ttsettings.cpp

# Both carry Q_OBJECT and therefore need moc.
HEADERS += \
    ../../extern/ttffmpegwrapper.h \
    ../../common/ttsettings.h

CONFIG    += link_pkgconfig
PKGCONFIG += libavformat libavcodec libavutil libswscale libavfilter libswresample
