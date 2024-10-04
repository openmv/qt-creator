add_subdirectory(cplusplus)
add_subdirectory(syntax-highlighting)
add_subdirectory(libvterm)
add_subdirectory(libptyqt)
add_subdirectory(qrcodegen)
# OPENMV-DIFF #
# add_subdirectory(qtkeychain)
# add_subdirectory(lua)
# add_subdirectory(sol2)
# OPENMV-DIFF #

if(WIN32)
    add_subdirectory(winpty)
endif()
