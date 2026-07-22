add_qtc_plugin(OpenMV
  DEPENDS Qt::PrintSupport Qt::SerialPort Qt::Svg
  PLUGIN_DEPENDS Core TextEditor Python Copilot
  SOURCES
    openmvplugin.cpp openmvplugin.h openmvtr.h
    openmvpluginconnect.cpp openmvpluginconnect.h
    views/openmvboardinfoview.cpp views/openmvboardinfoview.h
    views/openmvchannelrecorder.cpp views/openmvchannelrecorder.h
    views/openmvchannelsview.cpp views/openmvchannelsview.h
    views/openmvmemoryview.cpp views/openmvmemoryview.h
    views/openmvstatisticsview.cpp views/openmvstatisticsview.h
    views/openmvviewstyle.cpp views/openmvviewstyle.h
    openmvmodelzoo.cpp openmvmodelzoo.h
    openmvromfs.cpp openmvromfs.h
    openmvthirdparty.cpp openmvthirdparty.h
    openmvthirdpartysettings.cpp openmvthirdpartysettings.h
    bootloaders/openmvpluginbootloader.cpp
    bootloaders/openmvplugindfubootloader.cpp
    bootloaders/openmvpluginimxbootloader.cpp
    bootloaders/openmvpluginalifbootloader.cpp
    bootloaders/openmvpluginarduinodfubootloader.cpp
    bootloaders/openmvpluginbossacbootloader.cpp
    bootloaders/openmvpluginpicotoolbootloader.cpp
    openmvpluginprotocol.cpp
    openmvpluginparse.cpp
    openmvplugindoc.cpp
    openmveject.cpp openmveject.h
    openmvpluginserialport.cpp openmvpluginserialport.h
    openmvpluginio.cpp openmvpluginio.h
    views/openmvpluginfb.cpp views/openmvpluginfb.h
    openmvprofile.cpp openmvprofile.h
    openmvterminal.cpp openmvterminal.h
    openmvdataseteditor.cpp openmvdataseteditor.h
    elfio/elfio/elfio.hpp
    views/openmvpluginhistogram.cpp views/openmvpluginhistogram.h
    views/rgb2rgb_tab.c
    views/lab_tab.c
    views/yuv_tab.c
    tools/driveserialnumber.cpp tools/driveserialnumber.h
    tools/myqserialportinfo.cpp tools/myqserialportinfo.h
    tools/loaderdialog.cpp tools/loaderdialog.h
    tools/mergedfilesystemmodel.cpp tools/mergedfilesystemmodel.h
    tools/alif.cpp tools/alif.h
    tools/bossac.cpp tools/bossac.h
    tools/dfu-util.cpp tools/dfu-util.h
    tools/edgeimpulse.cpp tools/edgeimpulse.h
    tools/hardwaremonitor.cpp tools/hardwaremonitor.h
    tools/imx.cpp tools/imx.h
    tools/keypointseditor.cpp tools/keypointseditor.h
    tools/mpy-cross.cpp tools/mpy-cross.h
    tools/picotool.cpp tools/picotool.h
    tools/romfs.cpp tools/romfs.h
    tools/settingseditor.cpp tools/settingseditor.h
    tools/wifidebug.cpp tools/wifidebug.h
    tools/wifiscan.cpp tools/wifiscan.h
    tools/stcubeprogrammer.cpp tools/stcubeprogrammer.h
    tools/stedgeai.cpp tools/stedgeai.h
    tools/tag16h5.c tools/tag16h5.h
    tools/tag25h7.c tools/tag25h7.h
    tools/tag25h9.c tools/tag25h9.h
    tools/tag36h10.c tools/tag36h10.h
    tools/tag36h11.c tools/tag36h11.h
    tools/tag36artoolkit.c tools/tag36artoolkit.h
    tools/thresholdeditor.cpp  tools/thresholdeditor.h
    tools/usbproblems.cpp tools/usbproblems.h
    tools/videotools.cpp tools/videotools.h
    tools/vela.cpp tools/vela.h
    qcustomplot/qcustomplot.cpp qcustomplot/qcustomplot.h
    qzip/qzip.cpp qzip/qzipreader.h qzip/qzipwriter.h
    openmv.qrc
    views/openmvpluginhistogram.ui
)

target_sources(OpenMV
  PRIVATE
    protocol/omv_buffer.cpp
    protocol/omv_buffer.h
    protocol/omv_camera.cpp
    protocol/omv_camera.h
    protocol/omv_constants.cpp
    protocol/omv_constants.h
    protocol/omv_crc.cpp
    protocol/omv_crc.h
    protocol/omv_exceptions.h
    protocol/omv_image.cpp
    protocol/omv_image.h
    protocol/omv_port.cpp
    protocol/omv_port.h
    protocol/omv_transport.cpp
    protocol/omv_transport.h
    protocol/omv_debug.cpp
    protocol/omv_debug.h
)

target_link_libraries(OpenMV PRIVATE Qt${QT_VERSION_MAJOR}::GuiPrivate)

if (WIN32)
    target_link_libraries(OpenMV PRIVATE setupapi cfgmgr32 user32)
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

if (APPLE)
    target_link_libraries(OpenMV PRIVATE
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework DiskArbitration"
    )
endif()

