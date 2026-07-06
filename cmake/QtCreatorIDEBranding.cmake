set(IDE_VERSION "14.0.2")                             # The IDE version.
set(IDE_VERSION_COMPAT "14.0.0")                      # The IDE Compatibility version.
set(IDE_VERSION_DISPLAY "14.0.2")                     # The IDE display version.
# OPENMV-DIFF #
set(IDE_AUTHOR "The Qt Company Ltd")
set(IDE_COPYRIGHT_YEAR_FOUNDED "2008")
# OPENMV-DIFF #
set(IDE_COPYRIGHT_YEAR "2024")                        # The IDE current copyright year.

set(IDE_SETTINGSVARIANT "QtProject")                  # The IDE settings variation.
set(IDE_DISPLAY_NAME "Qt Creator")                    # The IDE display name.
set(IDE_ID "qtcreator")                               # The IDE id (no spaces, lowercase!)
set(IDE_CASED_ID "QtCreator")                         # The cased IDE id (no spaces!)
set(IDE_BUNDLE_IDENTIFIER "org.qt-project.${IDE_ID}") # The macOS application bundle identifier.

set(PROJECT_USER_FILE_EXTENSION .user)
set(IDE_DOC_FILE "qtcreator/qtcreator.qdocconf")
set(IDE_DOC_FILE_ONLINE "qtcreator/qtcreator-online.qdocconf")

# Absolute, or relative to <qtcreator>/src/app
# Should contain qtcreator.ico, qtcreator.xcassets
set(IDE_ICON_PATH "")
# Absolute, or relative to <qtcreator>/src/app
# Should contain images/logo/(16|24|32|48|64|128|256|512)/QtProject-qtcreator.png
set(IDE_LOGO_PATH "")

# OPENMV-DIFF #
set(IDE_VERSION "5.0.0")
set(IDE_VERSION_COMPAT "${IDE_VERSION}")
set(IDE_VERSION_DISPLAY "${IDE_VERSION}")
set(IDE_AUTHOR "OpenMV LLC")
set(IDE_COPYRIGHT_YEAR_FOUNDED "2013")
set(IDE_COPYRIGHT_YEAR "2024")
set(IDE_SETTINGSVARIANT "OpenMV")
set(IDE_DISPLAY_NAME "OpenMV IDE")
set(IDE_ID "openmvide")
set(IDE_CASED_ID "OpenMVIDE")
set(IDE_BUNDLE_IDENTIFIER "io.openmv.${IDE_ID}")

# The viewer variant: a separate, forced-viewer-mode build (configure with
# -DOPENMV_VIEWER_IDE=ON). It keeps the "OpenMV" settings org but gets its own
# display name, executable id, settings store (via IDE_CASED_ID), and bundle id,
# so it installs and runs independently of the full IDE. The forced-viewer
# behaviour itself comes from the OPENMV_VIEWER_IDE compile definition (added in
# the top-level CMakeLists after project()).
if(OPENMV_VIEWER_IDE)
set(IDE_DISPLAY_NAME "OpenMV Viewer")
set(IDE_ID "openmvviewer")
set(IDE_CASED_ID "OpenMVViewer")
set(IDE_BUNDLE_IDENTIFIER "io.openmv.${IDE_ID}")
endif()
# OPENMV-DIFF #
