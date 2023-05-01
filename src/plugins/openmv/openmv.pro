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
           tools/bossac.h \
           tools/dfu-util.h \
           tools/edgeimpulse.h \
           tools/imx.h \
           tools/keypointseditor.h \
           tools/picotool.h \
           tools/tag16h5.h \
           tools/tag25h7.h \
           tools/tag25h9.h \
           tools/tag36h10.h \
           tools/tag36h11.h \
           tools/tag36artoolkit.h \
           tools/thresholdeditor.h \
           tools/videotools.h \
           qcustomplot/qcustomplot.h
SOURCES += openmvplugin.cpp \
           openmvpluginconnect.cpp \
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
           histogram/yuv_tab.c \
           tools/bossac.cpp \
           tools/dfu-util.cpp \
           tools/edgeimpulse.cpp \
           tools/imx.cpp \
           tools/keypointseditor.cpp \
           tools/picotool.cpp \
           tools/tag16h5.c \
           tools/tag25h7.c \
           tools/tag25h9.c \
           tools/tag36h10.c \
           tools/tag36h11.c \
           tools/tag36artoolkit.c \
           tools/thresholdeditor.cpp \
           tools/videotools.cpp \
           qcustomplot/qcustomplot.cpp
FORMS += openmvcamerasettings.ui histogram/openmvpluginhistogram.ui
RESOURCES += openmv.qrc
