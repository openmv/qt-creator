add_qtc_plugin(OpenMV
  DEPENDS Qt::PrintSupport Qt::SerialPort
  PLUGIN_DEPENDS Core TextEditor
  SOURCES
    openmvplugin.cpp openmvplugin.h openmvtr.h
    openmvpluginconnect.cpp
    openmvpluginparse.cpp
    openmveject.cpp openmveject.h
    openmvpluginserialport.cpp openmvpluginserialport.h
    openmvpluginio.cpp openmvpluginio.h
    openmvpluginfb.cpp openmvpluginfb.h
    openmvterminal.cpp openmvterminal.h
    openmvcamerasettings.cpp openmvcamerasettings.h
    openmvdataseteditor.cpp openmvdataseteditor.h
    histogram/openmvpluginhistogram.cpp histogram/openmvpluginhistogram.h
    histogram/rgb2rgb_tab.c
    histogram/lab_tab.c
    histogram/yuv_tab.c
    tools/bossac.cpp tools/bossac.h
    tools/dfu-util.cpp tools/dfu-util.h
    tools/edgeimpulse.cpp tools/edgeimpulse.h
    tools/imx.cpp tools/imx.h
    tools/keypointseditor.cpp tools/keypointseditor.h
    tools/picotool.cpp tools/picotool.h
    tools/tag16h5.c tools/tag16h5.h
    tools/tag25h7.c tools/tag25h7.h
    tools/tag25h9.c tools/tag25h9.h
    tools/tag36h10.c tools/tag36h10.h
    tools/tag36h11.c tools/tag36h11.h
    tools/tag36artoolkit.c tools/tag36artoolkit.h
    tools/thresholdeditor.cpp  tools/thresholdeditor.h
    tools/videotools.cpp tools/videotools.h
    qcustomplot/qcustomplot.cpp qcustomplot/qcustomplot.h
    openmv.qrc
    openmvcamerasettings.ui
    histogram/openmvpluginhistogram.ui
)

target_link_libraries(OpenMV PRIVATE Qt${QT_VERSION_MAJOR}::GuiPrivate)
