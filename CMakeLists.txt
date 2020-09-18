cmake_minimum_required(VERSION 3.0)
project(minnet)

include(FindCURL)

# include: libwebsockets find_package(libwebsockets)
set(LIBWEBSOCKETS_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/libwebsockets/include
    ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets
    ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/include)
set(LIBWEBSOCKETS_FOUND TRUE)
set(LIBWEBSOCKETS_LIBRARIES websockets ${CURL_LIBRARIES})

include_directories(${LIBWEBSOCKETS_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
link_directories(${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/lib)

function(make_shared_module FNAME)
  string(REGEX REPLACE "_" "-" NAME "${FNAME}")
  string(TOUPPER "${FNAME}" UNAME)

  message("Module: ${NAME}")
  set(TARGET_NAME quickjs-${NAME})

  add_library(${TARGET_NAME} SHARED ${FNAME}.c)

  target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBS} quickjs dl)
  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES
      PREFIX ""
      BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
      INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
      OUTPUT_NAME "${NAME}"
      # COMPILE_FLAGS "-fvisibility=hidden"
      BUILD_RPATH
      "${CMAKE_BINARY_DIR};${CMAKE_CURRENT_BINARY_DIR};${CMAKE_BINARY_DIR}/quickjs;${CMAKE_CURRENT_BINARY_DIR}/quickjs"
  )
  target_compile_definitions(${TARGET_NAME} PRIVATE -DJS_${UNAME}_MODULE=1)
  install(TARGETS ${TARGET_NAME} DESTINATION lib/quickjs)

  if(LIBWEBSOCKETS_FOUND)
    target_include_directories(${TARGET_NAME}
                               PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBS})
  endif()
endfunction()
#
# file(GLOB JS_BINDINGS_SOURCES ${CMAKE_SOURCE_DIR}/src/js_*.cpp) foreach(MOD
# ${JS_BINDINGS_SOURCES}) string(REGEX REPLACE "\\.cpp" "" MOD "${MOD}")
# string(REGEX REPLACE ".*/js_" "" MOD "${MOD}") list(APPEND JS_BINDINGS_MODULES
# ${MOD}) endforeach(MOD ${JS_BINDINGS_SOURCES}) message("JS_BINDINGS_MODULES:
# ${JS_BINDINGS_MODULES}") foreach(JS_MODULE ${JS_BINDINGS_MODULES})
# make_shared_module(${JS_MODULE}) endforeach()

file(GLOB JS_BINDINGS_SOURCES minnet.c minnet.h)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(libwebsockets)

# Main
add_library(quickjs-minnet MODULE ${JS_BINDINGS_SOURCES})
target_include_directories(quickjs-minnet PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS}
                                                  ${CURL_INCLUDE_DIRS})
set_target_properties(
  quickjs-minnet
  PROPERTIES COMPILE_FLAGS "-fvisibility=hidden"
             PREFIX ""
             OUTPUT_NAME "minnet"
             OUTPUT_NAME_DEBUG "minnet-debug")
target_compile_definitions(quickjs-minnet PRIVATE "-DJS_BINDINGS_INIT_MODULE=1")
target_link_libraries(quickjs-minnet ${LIBWEBSOCKETS_LIBRARIES} quickjs)

install(TARGETS quickjs-minnet DESTINATION lib/quickjs)

# link
