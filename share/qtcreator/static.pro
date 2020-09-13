TEMPLATE = aux

include(../../qtcreator.pri)

STATIC_BASE = $$PWD
STATIC_OUTPUT_BASE = $$IDE_DATA_PATH
STATIC_INSTALL_BASE = $$INSTALL_DATA_PATH

#OPENMV-DIFF#
#DATA_DIRS = \
#    welcomescreen \
#    examplebrowser \
#    snippets \
#    templates \
#    themes \
#    designer \
#    schemes \
#    styles \
#    rss \
#    debugger \
#    qmldesigner \
#    qmlicons \
#    qml \
#    qml-type-descriptions \
#    modeleditor \
#    glsl \
#    cplusplus
#macx: DATA_DIRS += scripts
#OPENMV-DIFF#
DATA_DIRS = \
    examples \
    firmware \
    html \
    models \
    styles \
    themes
win32: DATA_DIRS += drivers dfuse ffmpeg/windows dfu-util/windows
else: DATA_DIRS += pydfu
macx: DATA_DIRS += ffmpeg/mac dfu-util/osx
linux-*:contains(QT_ARCH, i386): DATA_DIRS += ffmpeg/linux-x86 dfu-util/linux32
linux-*:contains(QT_ARCH, x86_64): DATA_DIRS += ffmpeg/linux-x86_64 dfu-util/linux64
linux-*:contains(QT_ARCH, arm): DATA_DIRS += ffmpeg/linux-arm dfu-util/arm
#OPENMV-DIFF#

for(data_dir, DATA_DIRS) {
    files = $$files($$PWD/$$data_dir/*, true)
    # Info.plist.in are handled below
    for(file, files):!contains(file, ".*/Info\\.plist\\.in$"):!exists($$file/*): \
        STATIC_FILES += $$file
}

include(../../qtcreatordata.pri)
