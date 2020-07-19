include(../../qtcreatorplugin.pri)
QT += concurrent gui-private network printsupport serialport qml
HEADERS += openmvplugin.h \
           openmveject.h \
           openmvpluginserialport.h \
           openmvpluginio.h \
           openmvpluginfb.h \
           openmvterminal.h \
           openmvcamerasettings.h \
           openmvdataseteditor.h \
           histogram/openmvpluginhistogram.h \
           tools/dfu-util.h \
           tools/thresholdeditor.h \
           tools/keypointseditor.h \
           tools/tag16h5.h \
           tools/tag25h7.h \
           tools/tag25h9.h \
           tools/tag36h10.h \
           tools/tag36h11.h \
           tools/tag36artoolkit.h \
           tools/videotools.h \
           qcustomplot/qcustomplot.h \
           qtrest/src/apibase.h \
           qtrest/src/models/abstractjsonrestlistmodel.h \
           qtrest/src/models/abstractxmlrestlistmodel.h \
           qtrest/src/models/baserestlistmodel.h \
           qtrest/src/models/detailsmodel.h \
           qtrest/src/models/jsonrestlistmodel.h \
           qtrest/src/models/pagination.h \
           qtrest/src/models/requests.h \
           qtrest/src/models/restitem.h \
           qtrest/src/models/xmlrestlistmodel.h
SOURCES += openmvplugin.cpp \
           openmveject.cpp \
           openmvpluginserialport.cpp \
           openmvpluginio.cpp \
           openmvpluginfb.cpp  \
           openmvterminal.cpp \
           openmvcamerasettings.cpp \
           openmvdataseteditor.cpp \
           histogram/openmvpluginhistogram.cpp \
           histogram/rgb2rgb_tab.c \
           histogram/lab_tab.c \
           histogram/yuv_tab.c  \
           tools/dfu-util.cpp \
           tools/thresholdeditor.cpp \
           tools/keypointseditor.cpp \
           tools/tag16h5.c \
           tools/tag25h7.c \
           tools/tag25h9.c \
           tools/tag36h10.c \
           tools/tag36h11.c \
           tools/tag36artoolkit.c \
           tools/videotools.cpp \
           qcustomplot/qcustomplot.cpp \
           qtrest/src/apibase.cpp \
           qtrest/src/models/abstractjsonrestlistmodel.cpp \
           qtrest/src/models/abstractxmlrestlistmodel.cpp \
           qtrest/src/models/baserestlistmodel.cpp \
           qtrest/src/models/detailsmodel.cpp \
           qtrest/src/models/jsonrestlistmodel.cpp \
           qtrest/src/models/pagination.cpp \
           qtrest/src/models/requests.cpp \
           qtrest/src/models/restitem.cpp \
           qtrest/src/models/xmlrestlistmodel.cpp
FORMS += openmvcamerasettings.ui histogram/openmvpluginhistogram.ui
RESOURCES += openmv.qrc
INCLUDEPATH += qtrest/src qtrest/src/models
