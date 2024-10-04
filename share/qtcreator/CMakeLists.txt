set(resource_directories
  # OPENMV-DIFF #
  # android
  # cplusplus
  # glsl
  # indexer_preincludes
  # jsonschemas
  # lua-plugins
  # modeleditor
  # qmldesigner
  # qmlicons
  # qml-type-descriptions
  # schemes
  # snippets
  # styles
  # templates
  # themes
  # OPENMV-DIFF #
  examples
  firmware
  html
  models
  # OPENMV-DIFF #
)

set(resource_files
  # OPENMV-DIFF #
  # debugger/.pylintrc
  # debugger/boosttypes.py
  # debugger/cdbbridge.py
  # debugger/cdbext.pyi
  # debugger/creatortypes.py
  # debugger/dumper.py
  # debugger/gdbbridge.py
  # debugger/gdbtracepoint.py
  # debugger/LICENSE.GPL3-EXCEPT
  # debugger/lldbbridge.py
  # debugger/misctypes.py
  # debugger/opencvtypes.py
  # debugger/pdbbridge.py
  # debugger/personaltypes.py
  # debugger/qttypes.py
  # debugger/README.txt
  # debugger/setup.cfg
  # debugger/android_stdtypes.py
  # debugger/libcpp_stdtypes.py
  # debugger/stdtypes.py
  # debugger/utils.py
  # debugger/loadorder.txt
  # OPENMV-DIFF #

  # OPENMV-DIFF #
  # debugger-with-python2/qttypes.py
  # debugger-with-python2/personaltypes.py
  # debugger-with-python2/android_stdtypes.py
  # debugger-with-python2/README.txt
  # debugger-with-python2/gdbbridge.py
  # debugger-with-python2/cdbbridge.py
  # debugger-with-python2/lldbbridge.py
  # debugger-with-python2/misctypes.py
  # debugger-with-python2/opencvtypes.py
  # debugger-with-python2/libcpp_stdtypes.py
  # debugger-with-python2/creatortypes.py
  # debugger-with-python2/stdtypes.py
  # debugger-with-python2/gdbtracepoint.py
  # debugger-with-python2/utils.py
  # debugger-with-python2/boosttypes.py
  # debugger-with-python2/dumper.py
  # debugger-with-python2/pdbbridge.py
  # OPENMV-DIFF #

  # OPENMV-DIFF #
  styles/flat-dark.xml
  styles/flat-light.xml
  themes/flat-dark.creatortheme
  themes/flat-light.creatortheme
  themes/dark.figmatokens
  themes/light.figmatokens
  # OPENMV-DIFF #
)

# OPENMV-DIFF #
if (WIN32)
  set(resource_directories ${resource_directories}
    drivers
    dfuse
    ffmpeg/windows
    dfu-util/windows
    bossac/windows
    picotool/windows
    blhost/win
    sdphost/win)
endif()

if (APPLE)
  set(resource_directories ${resource_directories}
    ffmpeg/mac
    dfu-util/osx
    bossac/osx
    picotool/osx
    blhost/mac
    sdphost/mac)
  set(resource_files ${resource_files} pydfu/pydfu.py)
endif()

if(UNIX AND NOT APPLE)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm*")
      set(resource_directories ${resource_directories}
        ffmpeg/linux-arm64
        dfu-util/aarch64
        bossac/aarch64
        picotool/aarch64
        pydfu)
    else()
      set(resource_directories ${resource_directories}
        ffmpeg/linux-x86_64
        dfu-util/linux64
        bossac/linux64
        picotool/linux64
        blhost/linux/amd64
        sdphost/linux/amd64
        pydfu)
    endif()
  else()
    if(${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm*")
      set(resource_directories ${resource_directories}
        ffmpeg/linux-armhf
        dfu-util/arm
        bossac/arm
        picotool/arm
        pydfu)
    else()
      set(resource_directories ${resource_directories}
        ffmpeg/linux-x86
        dfu-util/linux32
        bossac/linux32
        picotool/linux32
        sdphost/linux/i386
        pydfu)
    endif()
  endif()
endif()
# OPENMV-DIFF #

# copy resource directories during build
qtc_copy_to_builddir(copy_share_to_builddir
  DIRECTORIES ${resource_directories}
  DESTINATION "${IDE_DATA_PATH}"
  CREATE_SUBDIRS
)

# copy resource files during build
qtc_copy_to_builddir(copy_share_files_to_builddir
  FILES ${resource_files}
  DESTINATION ${IDE_DATA_PATH}
)

# create install rule for resource directories
# OPENMV-DIFF #
# install(
#   DIRECTORY ${resource_directories}
#   DESTINATION "${IDE_DATA_PATH}"
#   USE_SOURCE_PERMISSIONS
# )
# OPENMV-DIFF #
foreach(folder ${resource_directories})
  get_filename_component(path "${folder}" DIRECTORY)
  install(
    DIRECTORY "${folder}"
    DESTINATION "${IDE_DATA_PATH}/${path}"
    USE_SOURCE_PERMISSIONS
  )
endforeach()
# OPENMV-DIFF #

# create install rule for resource files
foreach(file ${resource_files})
  get_filename_component(path "${file}" DIRECTORY)
  install(
    FILES "${file}"
    DESTINATION "${IDE_DATA_PATH}/${path}"
  )
endforeach()


add_subdirectory(translations)
