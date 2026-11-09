QT += quick qml quickcontrols2 network svg
CONFIG += c++20
TARGET = things
TEMPLATE = app

DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

SOURCES += \
    src/main.cpp \
    src/Backend.cpp \
    src/DevTools.cpp \
    src/Operations.cpp \
    src/PackageModel.cpp \
    src/Privilege.cpp \
    src/Theme.cpp \
    src/ThemeIconProvider.cpp

HEADERS += \
    src/Backend.h \
    src/DevTools.h \
    src/Operations.h \
    src/Package.h \
    src/PackageModel.h \
    src/Privilege.h \
    src/Theme.h \
    src/ThemeIconProvider.h

RESOURCES += qml.qrc

isEmpty(PREFIX) {
    PREFIX = $$(HOME)/.local
}

target.path = $$PREFIX/bin
desktop.path = $$PREFIX/share/applications
desktop.files = data/things.desktop
icons.path = $$PREFIX/share/icons/hicolor/scalable/apps
icons.files = data/things.svg

INSTALLS += target desktop icons
