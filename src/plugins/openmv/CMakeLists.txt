add_qtc_plugin(OpenMV
  DEPENDS Qt::PrintSupport Qt::SerialPort
  PLUGIN_DEPENDS Core TextEditor Python Copilot
  SOURCES
    openmvplugin.cpp openmvplugin.h openmvtr.h
    openmvpluginconnect.cpp openmvpluginconnect.h
    openmvromfs.cpp openmvromfs.h
    bootloaders/openmvpluginbootloader.cpp
    bootloaders/openmvplugindfubootloader.cpp
    bootloaders/openmvpluginimxbootloader.cpp
    bootloaders/openmvpluginalifbootloader.cpp
    bootloaders/openmvpluginarduinodfubootloader.cpp
    bootloaders/openmvpluginbossacbootloader.cpp
    bootloaders/openmvpluginpicotoolbootloader.cpp
    openmvpluginparse.cpp
    openmvplugindoc.cpp
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
    tools/driveserialnumber.cpp tools/driveserialnumber.h
    tools/myqserialportinfo.cpp tools/myqserialportinfo.h
    tools/loaderdialog.cpp tools/loaderdialog.h
    tools/alif.cpp tools/alif.h
    tools/bossac.cpp tools/bossac.h
    tools/dfu-util.cpp tools/dfu-util.h
    tools/edgeimpulse.cpp tools/edgeimpulse.h
    tools/imx.cpp tools/imx.h
    tools/keypointseditor.cpp tools/keypointseditor.h
    tools/picotool.cpp tools/picotool.h
    tools/romfs.cpp tools/romfs.h
    tools/tag16h5.c tools/tag16h5.h
    tools/tag25h7.c tools/tag25h7.h
    tools/tag25h9.c tools/tag25h9.h
    tools/tag36h10.c tools/tag36h10.h
    tools/tag36h11.c tools/tag36h11.h
    tools/tag36artoolkit.c tools/tag36artoolkit.h
    tools/thresholdeditor.cpp  tools/thresholdeditor.h
    tools/videotools.cpp tools/videotools.h
    qcustomplot/qcustomplot.cpp qcustomplot/qcustomplot.h
    qzip/qzip.cpp qzip/qzipreader.h qzip/qzipwriter.h
    openmv.qrc
    openmvcamerasettings.ui
    histogram/openmvpluginhistogram.ui
)

target_sources(OpenMV
  PRIVATE
)

target_link_libraries(OpenMV PRIVATE Qt${QT_VERSION_MAJOR}::GuiPrivate)

if (WIN32)
    target_link_libraries(OpenMV PRIVATE Qt${QT_VERSION_MAJOR}::ZlibPrivate)
else()
    extend_qtc_plugin(OpenMV
      SOURCES
        zlib/zconf.h
        zlib/zlib.h
        zlib/crc32.h
        zlib/deflate.h
        zlib/gzguts.h
        zlib/inffast.h
        zlib/inffixed.h
        zlib/inflate.h
        zlib/inftrees.h
        zlib/trees.h
        zlib/zutil.h
        zlib/adler32.c
        zlib/compress.c
        zlib/crc32.c
        zlib/deflate.c
        zlib/gzclose.c
        zlib/gzlib.c
        zlib/gzread.c
        zlib/gzwrite.c
        zlib/inflate.c
        zlib/infback.c
        zlib/inftrees.c
        zlib/inffast.c
        zlib/trees.c
        zlib/uncompr.c
        zlib/zutil.c
    )
endif()
