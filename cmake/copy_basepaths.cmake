# Keep an existing development base-path configuration. A fresh build uses the
# same default configuration as the normal install step.
if(NOT EXISTS "${OUTPUT_DIR}/basepaths.lua")
    if(EXISTS "${SOURCE_DIR}/basepaths.lua")
        configure_file("${SOURCE_DIR}/basepaths.lua" "${OUTPUT_DIR}/basepaths.lua" COPYONLY)
    else()
        configure_file("${SOURCE_DIR}/basepaths.lua.sample" "${OUTPUT_DIR}/basepaths.lua" COPYONLY)
    endif()
endif()
