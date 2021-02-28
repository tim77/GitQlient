INCLUDEPATH += $$PWD

HEADERS += \
   $$PWD/Comment.h \
   $$PWD/Commit.h \
   $$PWD/ConfigData.h \
   $$PWD/GitHubRestApi.h \
   $$PWD/GitLabRestApi.h \
   $$PWD/GitServerCache.h \
   $$PWD/IRestApi.h \
   $$PWD/Issue.h \
   $$PWD/Label.h \
   $$PWD/Milestone.h \
   $$PWD/Platform.h \
   $$PWD/PullRequest.h \
   $$PWD/User.h

SOURCES += \
   $$PWD/GitServerCache.cpp \
   $$PWD/GitHubRestApi.cpp \
   $$PWD/GitLabRestApi.cpp \
   $$PWD/IRestApi.cpp
