cmake_minimum_required(VERSION 3.0)
project(minnet)

set(DISABLE_WERROR ON)
message("CURL: ${CURL}")

option(USE_SYSTEM_LIBCURL "Use system-installed libcurl" ON)
option(USE_SYSTEM_LIBWEBSOCKETS "Use system-installed libwebsockets" ON)


if(USE_SYSTEM_LIBCURL) 
  unset(LIBCURL_INCLUDE_DIRS CACHE)
  unset(LIBCURL_LIBRARY_DIRS CACHE)
  unset(LIBCURL_LIBRARIES CACHE)
  unset(LIBCURL_FOUND CACHE)

  if(NOT CURL_FOUND)
    include(FindCURL)
    include(FindPkgConfig)
    if(NOT CURL_FOUND)
      pkg_check_modules(LIBCURL libcurl)

      if(NOT LIBCURL_FOUND)
        find_library(
          LIBCURL
          NAMES libcurl curl
          PATHS "${CMAKE_INSTALL_PREFIX}/lib")
        set(LIBCURL_LIBRARIES ${LIBCURL})
      endif(NOT LIBCURL_FOUND)
    else(NOT CURL_FOUND)
      set(LIBCURL_LIBRARIES ${CURL_LIBRARIES})
      set(LIBCURL_INCLUDE_DIRS ${CURL_INCLUDE_DIRS})
      set(LIBCURL_LIBRARY_DIRS ${CMAKE_INSTALL_PREFIX}/lib)
      set(LIBCURL_FOUND ${CURL_FOUND})
    endif(NOT CURL_FOUND)
  endif(NOT CURL_FOUND)


  if(NOT CURL_FOUND)
    set(USE_SYSTEM_LIBCURL FALSE)
  endif(NOT CURL_FOUND)
endif(USE_SYSTEM_LIBCURL) 

if(NOT USE_SYSTEM_LIBCURL)
  message("-- Building CURL from source")
  include(ExternalProject)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/curl/CMakeLists.txt")
    set(CURL_REPO "")
    add_subdirectory(curl)
  else()
     set(CURL_REPO "https://github.com/curl/curl.git")

  ExternalProject_Add(
    curl
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/curl"
    GIT_REPOSITORY "${CURL_REPO}"
    GIT_SUBMODULES_RECURSE TRUE
    GIT_PROGRESS TRUE
    BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/curl"
    STEP_TARGETS   build)
  set_property(DIRECTORY PROPERTY EP_STEP_TARGETS build)
 endif()

  set(LIBCURL_LIBRARY_DIRS "${CMAKE_CURRENT_BINARY_DIR}/curl")
  set(LIBCURL_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/curl/include")
  set(LIBCURL_LIBRARIES curl)
endif()


foreach(VAR LIBCURL_LIBRARIES LIBCURL_INCLUDE_DIRS LIBCURL_LIBRARY_DIRS LIBCURL_FOUND)
  message("${VAR} = ${${VAR}}")
endforeach()


set(DISABLE_WERROR ON)

if(USE_SYSTEM_LIBWEBSOCKETS)
  include(FindPkgConfig)

  unset(LIBWEBSOCKETS_INCLUDE_DIRS CACHE)
  unset(LIBWEBSOCKETS_LIBRARY_DIRS CACHE)
  unset(LIBWEBSOCKETS_LIBRARIES CACHE)
  unset(LIBWEBSOCKETS_FOUND CACHE)

  pkg_search_module(LIBWEBSOCKETS libwebsockets)
endif(USE_SYSTEM_LIBWEBSOCKETS)



if(NOT QUICKJS_INCLUDE_DIR)
  set(QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif(NOT QUICKJS_INCLUDE_DIR)

if(QUICKJS_INCLUDE_DIR)
  include_directories(${QUICKJS_INCLUDE_DIR})
endif(QUICKJS_INCLUDE_DIR)

include_directories(${LIBWEBSOCKETS_INCLUDE_DIRS} ${LIBCURL_INCLUDE_DIRS})

function(make_shared_module FNAME)
  string(REGEX REPLACE "_" "-" NAME "${FNAME}")
  string(TOUPPER "${FNAME}" UNAME)

  message("Module: ${NAME}")
  set(TARGET_NAME quickjs-${NAME})

  add_library(${TARGET_NAME} SHARED ${FNAME}.c)

  target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES} ${LIBCURL_LIBRARIES} quickjs dl)
  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES PREFIX ""
               BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
               INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
               OUTPUT_NAME "${NAME}"
               # COMPILE_FLAGS "-fvisibility=hidden"
               BUILD_RPATH "${CMAKE_BINARY_DIR};${CMAKE_CURRENT_BINARY_DIR};${CMAKE_BINARY_DIR}/quickjs;${CMAKE_CURRENT_BINARY_DIR}/quickjs")
  target_compile_definitions(${TARGET_NAME} PRIVATE -DJS_${UNAME}_MODULE=1)
  install(TARGETS ${TARGET_NAME} DESTINATION lib/quickjs)

  if(LIBWEBSOCKETS_FOUND)
    target_include_directories(${TARGET_NAME} PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES})
  endif()
endfunction()


IF(NOT LIBWEBSOCKETS_FOUND)
  SET(USE_SYSTEM_LIBWEBSOCKETS OFF)
  message("-- Building LIBWEBSOCKETS_FOUND from source")

  set(LWS_WITHOUT_TESTAPPS TRUE)
  set(LWS_WITHOUT_TEST_SERVER TRUE)
  set(LWS_WITHOUT_TEST_PING TRUE)
  set(LWS_WITHOUT_TEST_CLIENT TRUE)

  # include: libwebsockets find_package(libwebsockets)
  set(LIBWEBSOCKETS_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/libwebsockets/include ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/include)
  set(LIBWEBSOCKETS_FOUND TRUE)
  set(LIBWEBSOCKETS_LIBRARIES websockets)
  set(LIBWEBSOCKETS_LIBRARY_DIRS ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/lib)
  #add_subdirectory(libwebsockets)
ELSE(NOT LIBWEBSOCKETS_FOUND)


ENDIF(NOT LIBWEBSOCKETS_FOUND)

foreach(VAR LIBWEBSOCKETS_LIBRARIES LIBWEBSOCKETS_INCLUDE_DIRS LIBWEBSOCKETS_LIBRARY_DIRS LIBWEBSOCKETS_FOUND)
  message("${VAR} = ${${VAR}}")
endforeach()

#if(NOT USE_SYSTEM_LIBWEBSOCKETS)
  link_directories(${LIBWEBSOCKETS_LIBRARY_DIRS})
#endif(NOT USE_SYSTEM_LIBWEBSOCKETS)

# file(GLOB JS_BINDINGS_SOURCES ${CMAKE_SOURCE_DIR}/src/js_*.cpp) foreach(MOD ${JS_BINDINGS_SOURCES}) string(REGEX REPLACE "\\.cpp" "" MOD "${MOD}") string(REGEX REPLACE ".*/js_" "" MOD "${MOD}") list(APPEND JS_BINDINGS_MODULES ${MOD}) endforeach(MOD ${JS_BINDINGS_SOURCES})
# message("JS_BINDINGS_MODULES: ${JS_BINDINGS_MODULES}") foreach(JS_MODULE ${JS_BINDINGS_MODULES}) make_shared_module(${JS_MODULE}) endforeach()

file(GLOB JS_BINDINGS_SOURCES minnet.c minnet.h)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT USE_SYSTEM_LIBWEBSOCKETS)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libwebsockets/CMakeLists.txt)
    add_subdirectory(libwebsockets)
  endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libwebsockets/CMakeLists.txt)
endif(NOT USE_SYSTEM_LIBWEBSOCKETS)
# Main
add_library(quickjs-minnet MODULE ${JS_BINDINGS_SOURCES})
target_include_directories(quickjs-minnet PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS} ${LIBCURL_INCLUDE_DIRS})
set_target_properties(
  quickjs-minnet
  PROPERTIES COMPILE_FLAGS "-fvisibility=hidden"
             PREFIX ""
             OUTPUT_NAME "net"
             # OUTPUT_NAME_DEBUG "net-debug"
)
target_compile_definitions(quickjs-minnet PRIVATE -DJS_SHARED_LIBRARY=1 -DJS_BINDINGS_INIT_MODULE=1)
target_link_libraries(quickjs-minnet ${LIBWEBSOCKETS_LIBRARIES} ${LIBCURL_LIBRARIES} quickjs)

install(TARGETS quickjs-minnet DESTINATION lib/quickjs)

# link
