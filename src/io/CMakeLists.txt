add_library(impacto_gpda STATIC gpdaarchive.cpp)
target_compile_features(impacto_gpda PUBLIC cxx_std_20)
target_include_directories(impacto_gpda PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
