QT += core gui widgets serialport network
CONFIG += c++11
TEMPLATE = app
TARGET = device_monitor
SOURCES += main.cpp protocol.cpp worker.cpp
HEADERS += protocol.h worker.h
QMAKE_CXXFLAGS += /utf-8
