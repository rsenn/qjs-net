macro(build_brotli)
  include(ExternalProject)

  ExternalProject_Add(libbrotli
    URL https://github.com/google/brotli/archive/v1.0.8.tar.gz
    PREFIX brotli
    CMAKE_ARGS 
              -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
              -DCMAKE_POSITION_INDEPENDENT_CODE=${CMAKE_POSITION_INDEPENDENT_CODE}
              -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
              -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
              -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
              -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
    CMAKE_GENERATOR   ${CMAKE_GENERATOR}
    CMAKE_GENERATOR_PLATFORM ${CMAKE_GENERATOR_PLATFORM}
    INSTALL_COMMAND   ""
  )

  ExternalProject_Get_Property(libbrotli SOURCE_DIR BINARY_DIR)

  add_library(brotlicommon STATIC IMPORTED GLOBAL)
  add_library(brotlidec STATIC IMPORTED GLOBAL)
  add_library(brotlienc STATIC IMPORTED GLOBAL)

  add_dependencies(brotlicommon libbrotli)
  add_dependencies(brotlidec libbrotli)
  add_dependencies(brotlienc libbrotli)

  if(MSVC)
    set_target_properties(brotlicommon PROPERTIES IMPORTED_LOCATION ${BINARY_DIR}/brotlicommon-static.lib )
    set_target_properties(brotlidec PROPERTIES IMPORTED_LOCATION ${BINARY_DIR}/brotlidec-static.lib )
    set_target_properties(brotlienc PROPERTIES IMPORTED_LOCATION ${BINARY_DIR}/brotlienc-static.lib )
  else()
    set_target_properties(brotlicommon PROPERTIES IMPORTED_LOCATION ${BINARY_DIR}/libbrotlicommon-static.a )
    set_target_properties(brotlidec PROPERTIES IMPORTED_LOCATION ${BINARY_DIR}/libbrotlidec-static.a )
    set_target_properties(brotlienc PROPERTIES IMPORTED_LOCATION ${BINARY_DIR}/libbrotlienc-static.a )
  endif()

  set(BROTLI_INCLUDE_DIR ${SOURCE_DIR}/c/include)
  set(BROTLI_LIBRARY brotlienc brotlidec brotlicommon)
endmacro(build_brotli)
