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
           tools/edgeimpulse.h \
           tools/thresholdeditor.h \
           tools/keypointseditor.h \
           tools/tag16h5.h \
           tools/tag25h7.h \
           tools/tag25h9.h \
           tools/tag36h10.h \
           tools/tag36h11.h \
           tools/tag36artoolkit.h \
           tools/videotools.h \
           qcustomplot/qcustomplot.h
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
           tools/edgeimpulse.cpp \
           tools/thresholdeditor.cpp \
           tools/keypointseditor.cpp \
           tools/tag16h5.c \
           tools/tag25h7.c \
           tools/tag25h9.c \
           tools/tag36h10.c \
           tools/tag36h11.c \
           tools/tag36artoolkit.c \
           tools/videotools.cpp \
           qcustomplot/qcustomplot.cpp
FORMS += openmvcamerasettings.ui histogram/openmvpluginhistogram.ui
RESOURCES += openmv.qrc
