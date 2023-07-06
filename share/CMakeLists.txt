add_subdirectory(qtcreator)

if (NOT APPLE AND NOT WIN32)
  set(DATE_ATTRIBUTE)
  if(SHOW_BUILD_DATE)
    string(TIMESTAMP timestamp "%Y-%m-%d")
    set(DATE_ATTRIBUTE " date=\"${timestamp}\"")
  endif()
  # OPENMV-DIFF #
  # configure_file(metainfo/org.qt-project.qtcreator.appdata.xml.cmakein metainfo/org.qt-project.qtcreator.appdata.xml)
  # OPENMV-DIFF #
  configure_file(metainfo/io.openmv.openmvide.appdata.xml.cmakein metainfo/io.openmv.openmvide.appdata.xml)
  # OPENMV-DIFF #

  # OPENMV-DIFF #
  # install(
  #   DIRECTORY
  #     applications
  #   DESTINATION
  #     ${CMAKE_INSTALL_DATAROOTDIR}
  # )
  # OPENMV-DIFF #
  install(
    FILES
      # OPENMV-DIFF #
      # ${CMAKE_CURRENT_BINARY_DIR}/metainfo/org.qt-project.qtcreator.appdata.xml
      # OPENMV-DIFF #
      ${CMAKE_CURRENT_BINARY_DIR}/metainfo/io.openmv.openmvide.appdata.xml
      # OPENMV-DIFF #
    DESTINATION
      ${CMAKE_INSTALL_DATAROOTDIR}/metainfo/
  )
endif()
