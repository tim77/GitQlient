QT -= gui
QT += widgets network webenginewidgets webchannel

TEMPLATE = lib
DEFINES += GITSERVER_LIBRARY QT_DEPRECATED_WARNINGS

CONFIG += qt warn_on c++ 17 c++1z

greaterThan(QT_MINOR_VERSION, 12) {
!msvc:QMAKE_CXXFLAGS += -Werror
}


# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

include(GitServer.pri)
include(GitServerWidgets.pri)

# Default rules for deployment.
unix {
    target.path = /usr/lib
}
!isEmpty(target.path): INSTALLS += target
