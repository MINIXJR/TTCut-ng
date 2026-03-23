TARGET = ttcut-esrepair
CONFIG += console
CONFIG -= qt app_bundle
QMAKE_CFLAGS += -std=c17 -Wall -Wextra
PKGCONFIG += libavcodec libavformat libavutil
SOURCES = ttcut-esrepair.c
