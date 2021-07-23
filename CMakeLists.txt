cmake_minimum_required(VERSION 3.0)
project(minnet)

set(DISABLE_WERROR TRUE CACHE BOOL "Disable -Werror flag" FORCE)

option(BUILD_SHARED_LIBWEBSOCKETS "Shared building of dependency libraries" OFF)
message("libwebsockets shared: ${BUILD_SHARED_LIBWEBSOCKETS}")

if(BUILD_SHARED_LIBWEBSOCKETS)
  set(LWS_WITH_SHARED TRUE)
  set(LWS_WITH_STATIC FALSE)
else(BUILD_SHARED_LIBWEBSOCKETS)
  set(LWS_WITH_SHARED FALSE)
  set(LWS_WITH_STATIC TRUE)
endif(BUILD_SHARED_LIBWEBSOCKETS)

set(LWS_WITH_LIBUV OFF)
set(LWS_STATIC_PIC TRUE)
set(LWS_WITH_EXTERNAL_POLL ON)
set(LWS_WITH_HTTP_BROTLI ON)
set(LWS_WITH_HTTP_PROXY ON)
set(LWS_WITH_HTTP_STREAM_COMPRESSION ON)
set(LWS_WITH_PLUGINS ON)
set(LWS_WITH_PLUGINS_API ON)
set(LWS_WITH_PLUGINS_BUILTIN ON)
set(LWS_WITH_SOCKS5 ON)
set(LWS_WITH_ZLIB ON)
set(LWS_WITH_ZIP_FOPS ON)
set(LWS_HAVE_LIBCAP FALSE)

if(BUILD_CURL)
  set(LOCAL_CURL ON)
else(BUILD_CURL)
  set(LOCAL_CURL OFF)
endif(BUILD_CURL)

set(CMAKE_BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}/curl/lib" CACHE PATH "Build with local rpath"
                                                                   FORCE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib" CACHE PATH "Build with install rpath" FORCE)

option(BUILD_CURL "Build curl library" ${LOCAL_CURL})
message("BUILD_CURL: ${BUILD_CURL}")

if(NOT BUILD_CURL)
  if(NOT CURL_FOUND)
    include(FindCURL)
    include(FindPkgConfig)
    if(NOT CURL_FOUND)
      pkg_check_modules(CURL libcurl)

      if(NOT CURL_FOUND)
        find_library(CURL NAMES libcurl curl PATHS "${CMAKE_INSTALL_PREFIX}/lib")
      endif(NOT CURL_FOUND)
    endif(NOT CURL_FOUND)
  endif(NOT CURL_FOUND)
endif(NOT BUILD_CURL)

if(NOT CURL_FOUND OR BUILD_CURL)
  set(BUILD_CURL TRUE CACHE BOOL "Build curl from source" FORCE)
  # set(LIBCURL_NO_SHARED libcurl CACHE STRING "Build static libcurl" FORCE)
  # set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "PIC code" FORCE) set(BUILD_SHARED_LIBS OFF
  # CACHE BOOL "Build shared libraries" FORCE)
  message("-- Building CURL from source")
  include(ExternalProject)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/curl/CMakeLists.txt")
    set(CURL_REPO "")
    # add_subdirectory(curl)
  else()
    set(CURL_REPO "https://github.com/curl/curl.git")

    ExternalProject_Add(
      curl
      SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/curl"
      GIT_REPOSITORY "${CURL_REPO}"
      GIT_SUBMODULES_RECURSE TRUE
      GIT_PROGRESS TRUE
      BINARY_DIR
        curl # STEP_TARGETS   build CONFIGURE_COMMAND cmake "${CMAKE_CURRENT_SOURCE_DIR}/curl"
             # -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF CMAKE_ARGS
             # -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF
             # -DCMAKE_POSITION_INDEPENDENT_CODE=ON CMAKE_CACHE_ARGS
             # -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF
    )
    set_property(DIRECTORY PROPERTY EP_STEP_TARGETS build)
  endif()

  if(CURL_LIBRARIES AND NOT CURL_LIBRARY)
    set(CURL_LIBRARY "${CURL_LIBRARIES}")
  else(LIBCURL_NO_SHARED AND NOT CURL_LIBRARY)
    set(CURL_LIBRARY curl)
  endif()
  set(CURL_LIBRARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/curl/lib")
  set(CURL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/curl/include")
  link_directories(${CMAKE_CURRENT_BINARY_DIR}/curl/lib)
endif()
message(STATUS "CURL library: ${CURL_LIBRARY}")
set(DISABLE_WERROR ON)

#[[option(USE_SYSTEM_LIBWEBSOCKETS "Use system-installed libwebsockets" ON)

if(USE_SYSTEM_LIBWEBSOCKETS)
  include(FindPkgConfig)

  unset(LIBWEBSOCKETS_INCLUDE_DIRS CACHE)
  unset(LIBWEBSOCKETS_LIBRARY_DIRS CACHE)
  unset(LIBWEBSOCKETS_LIBRARIES CACHE)
  unset(LIBWEBSOCKETS_FOUND CACHE)

  pkg_search_module(LIBWEBSOCKETS libwebsockets)
endif(USE_SYSTEM_LIBWEBSOCKETS)]]

if(NOT QUICKJS_INCLUDE_DIR)
  if(QUICKJS_INCLUDE_DIR)
    set(QUICKJS_INCLUDE_DIR "${QUICKJS_INCLUDE_DIR}")
  else(QUICKJS_INCLUDE_DIR)
    set(QUICKJS_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")

  endif(QUICKJS_INCLUDE_DIR)
endif(NOT QUICKJS_INCLUDE_DIR)

if(EXISTS "${QUICKJS_INCLUDE_DIR}/quickjs")
  set(QUICKJS_INCLUDE_DIR "${QUICKJS_INCLUDE_DIR}/quickjs")
endif(EXISTS "${QUICKJS_INCLUDE_DIR}/quickjs")

# if(NOT EXISTS "${QUICKJS_INCLUDE_DIR}") message(FATAL_ERROR "No QuickJS include directory found!")
# endif(NOT EXISTS "${QUICKJS_INCLUDE_DIR}")

include(CheckIncludeFile)

message("QUICKJS_INCLUDE_DIR=${QUICKJS_INCLUDE_DIR}")
# message("CMAKE_SYSTEM_INCLUDE_PATH=${CMAKE_SYSTEM_INCLUDE_PATH}")

if(EXISTS "${QUICKJS_INCLUDE_DIR}")
  list(APPEND CMAKE_REQUIRED_INCLUDES "${QUICKJS_INCLUDE_DIR}")
endif(EXISTS "${QUICKJS_INCLUDE_DIR}")

check_include_file("quickjs.h" HAVE_QUICKJS_H)

if(HAVE_QUICKJS_H AND QUICKJS_INCLUDE_DIR)
  include_directories(${QUICKJS_INCLUDE_DIR})
endif(HAVE_QUICKJS_H AND QUICKJS_INCLUDE_DIR)

include_directories(${LIBWEBSOCKETS_INCLUDE_DIRS} ${CURL_INCLUDE_DIR})

function(make_shared_module FNAME)
  string(REGEX REPLACE "_" "-" NAME "${FNAME}")
  string(TOUPPER "${FNAME}" UNAME)

  message("Module: ${NAME}")
  set(TARGET_NAME quickjs-${NAME})

  add_library(${TARGET_NAME} SHARED ${FNAME}.c)

  target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES} ${CURL_LIBRARY}
                        ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} quickjs dl)
  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES
      PREFIX ""
      BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
      INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
      OUTPUT_NAME "${NAME}" # COMPILE_FLAGS "-fvisibility=hidden"
      BUILD_RPATH
      "${CMAKE_BINARY_DIR};${CMAKE_CURRENT_BINARY_DIR};${CMAKE_BINARY_DIR}/quickjs;${CMAKE_CURRENT_BINARY_DIR}/quickjs"
  )
  target_compile_definitions(${TARGET_NAME} PRIVATE -DJS_${UNAME}_MODULE=1)
  install(TARGETS ${TARGET_NAME} DESTINATION lib/quickjs)

  if(LIBWEBSOCKETS_FOUND)
    target_include_directories(${TARGET_NAME} PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES} ${CURL_LIBRARY}
                          ${OPENSSL_CRYPTO_LIBRARY})
  endif()
endfunction()

option(BUILD_LIBWEBSOCKETS "build 3rdparty libwebsockets" OFF)

if(NOT BUILD_LIBWEBSOCKETS)
  include(FindPkgConfig)

  unset(LIBWEBSOCKETS_INCLUDE_DIRS CACHE)
  unset(LIBWEBSOCKETS_LIBRARY_DIRS CACHE)
  unset(LIBWEBSOCKETS_LIBRARIES CACHE)
  unset(LIBWEBSOCKETS_FOUND CACHE)

  pkg_search_module(LIBWEBSOCKETS libwebsockets)
endif(NOT BUILD_LIBWEBSOCKETS)

if(NOT LIBWEBSOCKETS_FOUND)
  set(BUILD_LIBWEBSOCKETS ON CACHE BOOL "build 3rdparty libwebsockets" FORCE)
endif(NOT LIBWEBSOCKETS_FOUND)

if(BUILD_LIBWEBSOCKETS)
  message("-- Building LIBWEBSOCKETS from source")
  set(LWS_WITHOUT_TESTAPPS TRUE)
  set(LWS_WITHOUT_TEST_SERVER TRUE)
  set(LWS_WITHOUT_TEST_PING TRUE)
  set(LWS_WITHOUT_TEST_CLIENT TRUE)
  set(LWS_LINK_TESTAPPS_DYNAMIC OFF CACHE BOOL "link test apps dynamic" FORCE)
  set(LWS_WITH_STATIC ON CACHE BOOL "build libwebsockets static library" FORCE)
  set(LWS_HAVE_LIBCAP FALSE CACHE BOOL "have libcap" FORCE)

  # include: libwebsockets find_package(libwebsockets)
  set(LIBWEBSOCKETS_INCLUDE_DIRS
      ${CMAKE_CURRENT_SOURCE_DIR}/libwebsockets/include ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets
      ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/include)
  set(LIBWEBSOCKETS_FOUND ON CACHE BOOL "found libwebsockets" FORCE)
  set(LIBWEBSOCKETS_LIBRARIES "mbedtls;uv;brotlienc;brotlidec;cap"
      CACHE STRING "libwebsockets libraries" FORCE)
  if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/lib/libwebsockets.a")
    list(PREPEND LIBWEBSOCKETS_LIBRARIES
         "${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/lib/libwebsockets.a")
  else(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/lib/libwebsockets.a")
    list(PREPEND LIBWEBSOCKETS_LIBRARIES "websockets")
  endif(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/lib/libwebsockets.a")

  set(LIBWEBSOCKETS_INCLUDE_DIRS "${LIBWEBSOCKETS_INCLUDE_DIRS}"
      CACHE PATH "libwebsockets include directory" FORCE)
  set(LIBWEBSOCKETS_LIBRARY_DIRS ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/lib
      CACHE PATH "libwebsockets library directory" FORCE)
  # add_subdirectory(libwebsockets ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets)
  include(ExternalProject)
  ExternalProject_Add(
    libwebsockets
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libwebsockets
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libwebsockets
    CMAKE_CACHE_ARGS "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER};-DCOMPILER_IS_CLANG:BOOL=OFF"
    CMAKE_CACHE_DEFAULT_ARGS
      "-DLWS_WITH_LIBUV:BOOL=OFF;-DLWS_STATIC_PIC:BOOL=ON;-DLWS_WITH_EXTERNAL_POLL:BOOL=ON;-DLWS_WITH_DISKCACHE:BOOL=ON;-DLWS_WITH_PLUGINS:BOOL=ON;-DLWS_WITH_PLUGINS_API:BOOL=ON;-DLWS_WITH_SOCKS5:BOOL=ON;-DLWS_WITH_ZIP_FOPS:BOOL=ON;-DLWS_WITH_ZLIB:BOOL=ON;-DDISABLE_WERROR:BOOL=ON;-DLWS_STATIC_PIC:BOOL=ON;-DLWS_WITH_SHARED:BOOL=OFF;-DLWS_WITH_STATIC:BOOL=ON;-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE};-DLWS_WITH_MBEDTLS:BOOL=ON;-DLWS_WITH_NO_LOGS:BOOL=ON;-DLWS_WITH_HTTP_BROTLI:BOOL=ON;-DLWS_WITH_HTTP_PROXY:BOOL=ON;-DLWS_WITH_HTTP_STREAM_COMPRESSION:BOOL=ON;-DLWS_HAVE_LIBCAP:BOOL=FALSE"
  )
  ExternalProject_Get_Property(libwebsockets CMAKE_CACHE_DEFAULT_ARGS)
  message("CMAKE_CACHE_DEFAULT_ARGS of libwebsockets = ${CMAKE_CACHE_DEFAULT_ARGS}")
  link_directories("${CMAKE_CURRENT_BINARY_DIR}/libwebsockets/lib")
endif(BUILD_LIBWEBSOCKETS)

foreach(VAR LIBWEBSOCKETS_LIBRARIES LIBWEBSOCKETS_INCLUDE_DIRS LIBWEBSOCKETS_LIBRARY_DIRS
            LIBWEBSOCKETS_FOUND)
  message("${VAR} = ${${VAR}}")
endforeach()

# if(NOT USE_SYSTEM_LIBWEBSOCKETS)
link_directories(${LIBWEBSOCKETS_LIBRARY_DIRS})
# endif(NOT USE_SYSTEM_LIBWEBSOCKETS)

if(NOT QUICKJS_INCLUDE_DIR)
  set(QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
endif(NOT QUICKJS_INCLUDE_DIR)

if(QUICKJS_INCLUDE_DIR)
  include_directories(${QUICKJS_INCLUDE_DIR})
endif(QUICKJS_INCLUDE_DIR)

include_directories(${LIBWEBSOCKETS_INCLUDE_DIRS} ${CURL_INCLUDE_DIR})

function(make_shared_module FNAME)
  string(REGEX REPLACE "_" "-" NAME "${FNAME}")
  string(TOUPPER "${FNAME}" UNAME)

  message("Module: ${NAME}")
  set(TARGET_NAME quickjs-${NAME})

  add_library(${TARGET_NAME} SHARED ${FNAME}.c)

  target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES} ${CURL_LIBRARY}
                        ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} dl)
  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES
      PREFIX ""
      BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}"
      INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib"
      OUTPUT_NAME "${NAME}" # COMPILE_FLAGS "-fvisibility=hidden"
      BUILD_RPATH
      "${CMAKE_BINARY_DIR};${CMAKE_CURRENT_BINARY_DIR};${CMAKE_BINARY_DIR}/quickjs;${CMAKE_CURRENT_BINARY_DIR}/quickjs"
  )
  target_compile_definitions(${TARGET_NAME} PRIVATE -DJS_${UNAME}_MODULE=1)
  install(TARGETS ${TARGET_NAME} DESTINATION lib/quickjs)

  if(LIBWEBSOCKETS_FOUND)
    target_include_directories(${TARGET_NAME} PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES} mbedtls)
  endif()
endfunction()

# if(NOT USE_SYSTEM_LIBWEBSOCKETS)
link_directories(${LIBWEBSOCKETS_LIBRARY_DIRS})
# endif(NOT USE_SYSTEM_LIBWEBSOCKETS)

# file(GLOB JS_BINDINGS_SOURCES ${CMAKE_SOURCE_DIR}/src/js_*.cpp) foreach(MOD
# ${JS_BINDINGS_SOURCES}) string(REGEX REPLACE "\\.cpp" "" MOD "${MOD}") string(REGEX REPLACE
# ".*/js_" "" MOD "${MOD}") list(APPEND JS_BINDINGS_MODULES ${MOD}) endforeach(MOD
# ${JS_BINDINGS_SOURCES}) message("JS_BINDINGS_MODULES: ${JS_BINDINGS_MODULES}") foreach(JS_MODULE
# ${JS_BINDINGS_MODULES}) make_shared_module(${JS_MODULE}) endforeach()

file(GLOB JS_BINDINGS_SOURCES minnet.c minnet.h server.c server.h)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Main
add_library(quickjs-minnet MODULE ${JS_BINDINGS_SOURCES})
target_include_directories(quickjs-minnet PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS} ${CURL_INCLUDE_DIR})
set_target_properties(
  quickjs-minnet PROPERTIES COMPILE_FLAGS "-fvisibility=hidden" PREFIX ""
                            OUTPUT_NAME "net" # OUTPUT_NAME_DEBUG "net-debug"
)
target_compile_definitions(quickjs-minnet PRIVATE -DJS_SHARED_LIBRARY=1 -DJS_BINDINGS_INIT_MODULE=1)
target_link_libraries(quickjs-minnet ${LIBWEBSOCKETS_LIBRARIES} ${CURL_LIBRARY}
                      ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})

install(TARGETS quickjs-minnet DESTINATION lib/quickjs
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ)

# link
