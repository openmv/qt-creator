# Level 0:
add_subdirectory(coreplugin)

# Level 1: (only depends of Level 0)
# OPENMV-DIFF #
# add_subdirectory(lua)
# OPENMV-DIFF #
add_subdirectory(texteditor)
# OPENMV-DIFF #
# add_subdirectory(serialterminal)
# add_subdirectory(extensionmanager)
# add_subdirectory(helloworld)
# OPENMV-DIFF #
add_subdirectory(imageviewer)
# OPENMV-DIFF #
# add_subdirectory(marketplace)
# add_subdirectory(screenrecorder)
# add_subdirectory(updateinfo)
# add_subdirectory(welcome)
# OPENMV-DIFF #

# Level 2: (only depends on Level 1 and below)
add_subdirectory(bineditor)
# OPENMV-DIFF #
# add_subdirectory(cpaster)
# OPENMV-DIFF #
add_subdirectory(diffeditor)
# OPENMV-DIFF #
# add_subdirectory(emacskeys)
# add_subdirectory(macros)
# OPENMV-DIFF #
add_subdirectory(projectexplorer)
# OPENMV-DIFF #
# add_subdirectory(silversearcher)
# OPENMV-DIFF #

# Level 3: (only depends on Level 2 and below)
# OPENMV-DIFF #
# add_subdirectory(axivion)
# add_subdirectory(compilerexplorer)
# add_subdirectory(cppeditor)
# add_subdirectory(haskell)
# add_subdirectory(help)
# OPENMV-DIFF #
add_subdirectory(resourceeditor)
# OPENMV-DIFF #
# add_subdirectory(nim)
# add_subdirectory(conan)
# add_subdirectory(saferenderer)
# add_subdirectory(terminal)
# add_subdirectory(vcpkg)
# OPENMV-DIFF #
add_subdirectory(tabbededitor)
# OPENMV-DIFF #

# Level 4: (only depends on Level 3 and below)
# OPENMV-DIFF #
# add_subdirectory(classview)
# add_subdirectory(glsleditor)
# add_subdirectory(modeleditor)
# OPENMV-DIFF #
add_subdirectory(qtsupport)
# OPENMV-DIFF #
# add_subdirectory(todo)
# add_subdirectory(vcsbase)
# OPENMV-DIFF #

# Level 5:
# OPENMV-DIFF #
# add_subdirectory(autotoolsprojectmanager)
# add_subdirectory(bazaar)
# add_subdirectory(beautifier)
# add_subdirectory(clearcase)
# add_subdirectory(cvs)
# add_subdirectory(designer)
# add_subdirectory(docker)
# add_subdirectory(fakevim)
# add_subdirectory(fossil)
# add_subdirectory(genericprojectmanager)
# add_subdirectory(git)
# add_subdirectory(mercurial)
# add_subdirectory(mesonprojectmanager)
# add_subdirectory(perforce)
# add_subdirectory(qmakeprojectmanager)
# add_subdirectory(qmljstools)
# add_subdirectory(scxmleditor)
# add_subdirectory(subversion)
# add_subdirectory(compilationdatabaseprojectmanager)
# OPENMV-DIFF #
add_subdirectory(languageclient)
# OPENMV-DIFF #
# if (WITH_QMLDESIGNER)
#   add_subdirectory(qmldesignerbase)
# endif()
# add_subdirectory(webassembly)
# OPENMV-DIFF #

# Level 6:
# OPENMV-DIFF #
# add_subdirectory(debugger)
# add_subdirectory(coco)
# OPENMV-DIFF #
add_subdirectory(copilot)
# OPENMV-DIFF #
# add_subdirectory(gitlab)
# if (WITH_QMLDESIGNER)
#   add_subdirectory(qmlprojectmanager)
# endif()
# OPENMV-DIFF #
add_subdirectory(python)
# OPENMV-DIFF #
# add_subdirectory(clangformat)
# add_subdirectory(cmakeprojectmanager)
# OPENMV-DIFF #
add_subdirectory(openmv)
# OPENMV-DIFF #

# Level 7:
# OPENMV-DIFF #
# add_subdirectory(android)
# add_subdirectory(autotest)
# add_subdirectory(baremetal)
# add_subdirectory(clangcodemodel)
# add_subdirectory(clangtools)
# add_subdirectory(cppcheck)
# add_subdirectory(incredibuild)
# add_subdirectory(ios)
# add_subdirectory(qmljseditor)
# add_subdirectory(qmlpreview)
# add_subdirectory(qmlprofiler)
# add_subdirectory(remotelinux)
# add_subdirectory(valgrind)
# add_subdirectory(perfprofiler)
# add_subdirectory(qbsprojectmanager)
# add_subdirectory(ctfvisualizer)
# add_subdirectory(squish)
# add_subdirectory(appstatisticsmonitor)
# OPENMV-DIFF #

# Level 8:
# OPENMV-DIFF #
# add_subdirectory(boot2qt)
# if (WITH_QMLDESIGNER)
#   unset(qmldesigner_builddir)
#   if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
#     # Workaround for @CMakeFiles\QmlDesigner.rsp ld.lld.exe: The filename or extension is too long.
#     # Clang on Windows is having problems with QmlDesigner.rsp which is bigger than 32KiB
#     set(qmldesigner_builddir ${PROJECT_BINARY_DIR}/qmldsgnr)
#   endif()
#   add_subdirectory(qmldesigner ${qmldesigner_builddir})
#   add_subdirectory(qmldesignerlite)
#   add_subdirectory(effectcomposer)
#   add_subdirectory(studiowelcome)
#   add_subdirectory(insight)
# endif()
# add_subdirectory(qnx)
# add_subdirectory(mcusupport)
# add_subdirectory(qtapplicationmanager)
# OPENMV-DIFF #
