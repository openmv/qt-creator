add_subdirectory(qtcreator)

if (NOT APPLE AND NOT WIN32)
  set(DATE_ATTRIBUTE)
  if(SHOW_BUILD_DATE)
    string(TIMESTAMP timestamp "%Y-%m-%d")
    set(DATE_ATTRIBUTE " date=\"${timestamp}\"")
  endif()
  # OPENMV-DIFF #
  # configure_file(metainfo/${IDE_APP_ID}.appdata.xml.cmakein metainfo/${IDE_APP_ID}.appdata.xml)
  # OPENMV-DIFF #
  configure_file(metainfo/io.openmv.openmvide.appdata.xml.cmakein metainfo/io.openmv.openmvide.appdata.xml)
  # OPENMV-DIFF #

  install(
    # OPENMV-DIFF #
    # FILES
    #   applications/${IDE_APP_ID}.desktop
    # DESTINATION
    #   ${CMAKE_INSTALL_DATAROOTDIR}/applications/
    # OPENMV-DIFF #
    FILES
      ${CMAKE_CURRENT_SOURCE_DIR}/applications/io.openmv.openmvide.desktop
    DESTINATION
      ${CMAKE_INSTALL_DATAROOTDIR}/applications/
    # OPENMV-DIFF #
  )
  install(
    FILES
    # OPENMV-DIFF #
    # ${CMAKE_CURRENT_BINARY_DIR}/metainfo/${IDE_APP_ID}.appdata.xml
    # OPENMV-DIFF #
    ${CMAKE_CURRENT_BINARY_DIR}/metainfo/io.openmv.openmvide.appdata.xml
    # OPENMV-DIFF #
    DESTINATION
      ${CMAKE_INSTALL_DATAROOTDIR}/metainfo/
  )
endif()
