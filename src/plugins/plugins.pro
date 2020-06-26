include(../../qtcreator.pri)

TEMPLATE  = subdirs

#OPENMV-DIFF#
#SUBDIRS   = \
#    autotest \
#    clangstaticanalyzer \
#    coreplugin \
#    qmlprofilerextension \
#    texteditor \
#    cppeditor \
#    bineditor \
#    diffeditor \
#    imageviewer \
#    bookmarks \
#    projectexplorer \
#    vcsbase \
#    perforce \
#    subversion \
#    git \
#    cvs \
#    cpptools \
#    qtsupport \
#    qmakeprojectmanager \
#    debugger \
#    help \
#    cpaster \
#    cmakeprojectmanager \
#    autotoolsprojectmanager \
#    fakevim \
#    emacskeys \
#    designer \
#    resourceeditor \
#    genericprojectmanager \
#    qmljseditor \
#    qmlprojectmanager \
#    glsleditor \
#    pythoneditor \
#    mercurial \
#    bazaar \
#    classview \
#    tasklist \
#    qmljstools \
#    macros \
#    remotelinux \
#    android \
#    valgrind \
#    todo \
#    qnx \
#    clearcase \
#    baremetal \
#    ios \
#    beautifier \
#    modeleditor \
#    qmakeandroidsupport \
#    winrt \
#    qmlprofiler \
#    updateinfo \
#    welcome
#OPENMV-DIFF#
SUBDIRS   = \
    coreplugin \
    texteditor \
    imageviewer \
    openmv
#OPENMV-DIFF#

#OPENMV-DIFF#
#DO_NOT_BUILD_QMLDESIGNER = $$(DO_NOT_BUILD_QMLDESIGNER)
#isEmpty(DO_NOT_BUILD_QMLDESIGNER) {
#    SUBDIRS += qmldesigner
#} else {
#    warning("QmlDesigner plugin has been disabled.")
#}
#OPENMV-DIFF#

#OPENMV-DIFF#
#isEmpty(QBS_INSTALL_DIR): QBS_INSTALL_DIR = $$(QBS_INSTALL_DIR)
#exists(../shared/qbs/qbs.pro)|!isEmpty(QBS_INSTALL_DIR): \
#    SUBDIRS += \
#        qbsprojectmanager
#OPENMV-DIFF#

#OPENMV-DIFF#
## prefer qmake variable set on command line over env var
#isEmpty(LLVM_INSTALL_DIR):LLVM_INSTALL_DIR=$$(LLVM_INSTALL_DIR)
#exists($$LLVM_INSTALL_DIR) {
#    SUBDIRS += clangcodemodel
#}
#OPENMV-DIFF#

#OPENMV-DIFF#
#isEmpty(IDE_PACKAGE_MODE) {
#    SUBDIRS += \
#        helloworld
#}
#OPENMV-DIFF#

for(p, SUBDIRS) {
    QTC_PLUGIN_DEPENDS =
    include($$p/$${p}_dependencies.pri)
    pv = $${p}.depends
    $$pv = $$QTC_PLUGIN_DEPENDS
}

#OPENMV-DIFF#
#linux-* {
#     SUBDIRS += debugger/ptracepreload.pro
#}
#OPENMV-DIFF#
