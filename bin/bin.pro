TEMPLATE = app
#OPENMV-DIFF#
#TARGET = qtcreator.sh
#OPENMV-DIFF#
TARGET = openmvide.sh
TARGET += openmv.png
#OPENMV-DIFF#

include(../qtcreator.pri)

OBJECTS_DIR =

#OPENMV-DIFF#
#PRE_TARGETDEPS = $$PWD/qtcreator.sh
#OPENMV-DIFF#
PRE_TARGETDEPS = $$PWD/openmvide.sh
PRE_TARGETDEPS += $$PWD/openmv.png
#OPENMV-DIFF#

#OPENMV-DIFF#
#QMAKE_LINK = cp $$PWD/qtcreator.sh $@ && : IGNORE REST OF LINE:
#OPENMV-DIFF#
QMAKE_LINK = cp $$PWD/openmvide.sh $@ && : IGNORE REST OF LINE:
QMAKE_LINK += cp $$PWD/openmv.png $@ && : IGNORE REST OF LINE:
#OPENMV-DIFF#
QMAKE_STRIP =
CONFIG -= qt separate_debug_info gdb_dwarf_index

#OPENMV-DIFF#
#QMAKE_CLEAN = qtcreator.sh
#OPENMV-DIFF#
QMAKE_CLEAN = openmvide.sh
QMAKE_CLEAN += openmv.png
#OPENMV-DIFF#

target.path  = $$INSTALL_BIN_PATH
INSTALLS    += target

#OPENMV-DIFF#
#DISTFILES = $$PWD/qtcreator.sh
#OPENMV-DIFF#
DISTFILES = $$PWD/openmvide.sh
DISTFILES += $$PWD/openmv.png
#OPENMV-DIFF#
