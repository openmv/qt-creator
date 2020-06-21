TEMPLATE = app
#OPENMV-DIFF#
#TARGET = qtcreator.sh
#OPENMV-DIFF#
TARGET = openmvide.sh
#OPENMV-DIFF#

include(../qtcreator.pri)

OBJECTS_DIR =

#OPENMV-DIFF#
#PRE_TARGETDEPS = $$PWD/qtcreator.sh
#OPENMV-DIFF#
PRE_TARGETDEPS = $$PWD/openmvide.sh
#OPENMV-DIFF#

#OPENMV-DIFF#
#QMAKE_LINK = cp $$PWD/qtcreator.sh $@ && : IGNORE REST OF LINE:
#OPENMV-DIFF#
QMAKE_LINK = cp $$PWD/openmvide.sh $@ && : IGNORE REST OF LINE:
#OPENMV-DIFF#
QMAKE_STRIP =
CONFIG -= qt separate_debug_info gdb_dwarf_index

#OPENMV-DIFF#
#QMAKE_CLEAN = qtcreator.sh
#OPENMV-DIFF#
QMAKE_CLEAN = openmvide.sh
#OPENMV-DIFF#

target.path  = $$INSTALL_BIN_PATH
INSTALLS    += target openmv.png

#OPENMV-DIFF#
#DISTFILES = $$PWD/qtcreator.sh
#OPENMV-DIFF#
DISTFILES = $$PWD/openmvide.sh
#OPENMV-DIFF#
