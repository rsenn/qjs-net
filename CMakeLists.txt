cmake_minimum_required(VERSION 3.0)
project(minnet)

set(DISABLE_WERROR TRUE CACHE BOOL "Disable -Werror flag")

option(BUILD_SHARED_LIBWEBSOCKETS "Shared building of dependency libraries" OFF)
#message("libwebsockets shared: ${BUILD_SHARED_LIBWEBSOCKETS}")

set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "PIC code")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")

if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
elseif(NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "" CACHE PATH "Specify the output directory for libraries (default is lib)")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UseMultiArch.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindMbedTLS.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildLibwebsockets.cmake)

message(STATUS "Finding QuickJS...")

find_quickjs()
configure_quickjs()

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
set(LWS_WITH_PLUGINS OFF)
set(LWS_WITH_PLUGINS_API OFF)
set(LWS_WITH_PLUGINS_BUILTIN OFF)
set(LWS_WITH_SOCKS5 ON)
set(LWS_WITH_ZLIB ON)
set(LWS_WITH_ZIP_FOPS ON)
set(LWS_HAVE_LIBCAP FALSE)

if(BUILD_CURL)
  set(LOCAL_CURL ON)
else(BUILD_CURL)
  set(LOCAL_CURL OFF)
endif(BUILD_CURL)

option(BUILD_CURL "Build curl library" ${LOCAL_CURL})
#message("BUILD_CURL: ${BUILD_CURL}")

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
  set(BUILD_CURL TRUE CACHE BOOL "Build curl from source")
  # set(LIBCURL_NO_SHARED libcurl CACHE STRING "Build static libcurl")
  message("-- Building CURL from source")
  include(ExternalProject)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/curl/CMakeLists.txt")
    set(CURL_REPO "")
    # add_subdirectory(curl)
  else()
    set(CURL_REPO "https://github.com/curl/curl.git")

    ExternalProject_Add(
      curl SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/curl" GIT_REPOSITORY "${CURL_REPO}" GIT_SUBMODULES_RECURSE TRUE GIT_PROGRESS TRUE BINARY_DIR curl STEP_TARGETS build CONFIGURE_COMMAND cmake "${CMAKE_CURRENT_SOURCE_DIR}/curl" -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF CMAKE_ARGS -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF
                                                                                                                                                                                                                                                                                                              -DCMAKE_POSITION_INDEPENDENT_CODE=ON CMAKE_CACHE_ARGS -DLIBCURL_NO_SHARED:STRING=libcurl
                                                                                                                                                                                                                                                                                                                                                                    -DBUILD_SHARED_LIBS:BOOL=OFF)
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

# if(NOT EXISTS "${QUICKJS_INCLUDE_DIR}") message(FATAL_ERROR "No QuickJS
# include directory found!") endif(NOT EXISTS "${QUICKJS_INCLUDE_DIR}")
include(CheckCCompilerFlag)

check_c_compiler_flag("-O0" OPT_C_OPT_NONE)
if(OPT_C_OPT_NONE)
  if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0" CACHE STRING "C compiler options")
  endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
endif(OPT_C_OPT_NONE)
check_c_compiler_flag("-ggdb" OPT_C_G_GDB)
if(OPT_C_G_GDB)
  if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb" CACHE STRING "C compiler options")
  endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
endif(OPT_C_G_GDB)

include(CheckIncludeFile)

#message("QUICKJS_INCLUDE_DIR=${QUICKJS_INCLUDE_DIR}")
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

  target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES} ${CURL_LIBRARY} mbedtls mbedx509 mbedcrypto ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} quickjs dl)
  set_target_properties(${TARGET_NAME} PROPERTIES PREFIX "" OUTPUT_NAME "${NAME}" # COMPILE_FLAGS "-fvisibility=hidden"
                                                  BUILD_RPATH "${MBEDTLS_LIBRARY_DIR};${CMAKE_BINARY_DIR};${CMAKE_CURRENT_BINARY_DIR};${CMAKE_BINARY_DIR}/quickjs;${CMAKE_CURRENT_BINARY_DIR}/quickjs")
  target_compile_definitions(${TARGET_NAME} PRIVATE -DJS_${UNAME}_MODULE=1)
  #message("C module dir: ${QUICKJS_C_MODULE_DIR}")
  install(TARGETS ${TARGET_NAME} DESTINATION ${QUICKJS_C_MODULE_DIR})

  if(LIBWEBSOCKETS_FOUND)
    target_include_directories(${TARGET_NAME} PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES} ${CURL_LIBRARY} mbedtls mbedx509 mbedcrypto ${OPENSSL_CRYPTO_LIBRARY})
  endif()
endfunction()

option(BUILD_LIBWEBSOCKETS "in-tree build of libwebsockets" ON)

if(NOT BUILD_LIBWEBSOCKETS)
  include(FindPkgConfig)

  unset(LIBWEBSOCKETS_INCLUDE_DIRS CACHE)
  unset(LIBWEBSOCKETS_LIBRARY_DIRS CACHE)
  unset(LIBWEBSOCKETS_LIBRARIES CACHE)
  unset(LIBWEBSOCKETS_FOUND CACHE)

  pkg_search_module(LIBWEBSOCKETS libwebsockets)

  if(EXISTS ${pkgcfg_lib_LIBWEBSOCKETS_websockets})
    set(LIBWEBSOCKETS_LIBRARIES "${pkgcfg_lib_LIBWEBSOCKETS_websockets}" CACHE FILEPATH "libwebsockets library")
  endif(EXISTS ${pkgcfg_lib_LIBWEBSOCKETS_websockets})

  if(LIBWEBSOCKETS_LIBRARIES AND LIBWEBSOCKETS_LIBRARIES MATCHES ".*/.*")
    string(REGEX REPLACE "/[^/]*$" "" LIBWEBSOCKETS_LIBRARY_DIRS "${LIBWEBSOCKETS_LIBRARIES}")
    string(REGEX REPLACE "/lib/.*$" "/include" LIBWEBSOCKETS_INCLUDE_DIRS "${LIBWEBSOCKETS_LIBRARIES}")
    set(LIBWEBSOCKETS_LIBRARY_DIRS "${LIBWEBSOCKETS_LIBRARY_DIRS}" CACHE PATH "libwebsockets library directory")
    set(LIBWEBSOCKETS_INCLUDE_DIRS "${LIBWEBSOCKETS_INCLUDE_DIRS}" CACHE PATH "libwebsockets include directory")
  endif(LIBWEBSOCKETS_LIBRARIES AND LIBWEBSOCKETS_LIBRARIES MATCHES ".*/.*")

  if(CMAKE_INSTALL_RPATH)
    set(CMAKE_INSTALL_RPATH "${LIBWEBSOCKETS_LIBRARY_DIRS}:${CMAKE_INSTALL_RPATH}" CACHE PATH "Install runtime path")
  else(CMAKE_INSTALL_RPATH)
    set(CMAKE_INSTALL_RPATH "${LIBWEBSOCKETS_LIBRARY_DIRS}" CACHE PATH "Install runtime path")
  endif(CMAKE_INSTALL_RPATH)

endif(NOT BUILD_LIBWEBSOCKETS)

if(NOT LIBWEBSOCKETS_FOUND)
  set(BUILD_LIBWEBSOCKETS ON CACHE BOOL "build 3rdparty libwebsockets")
endif(NOT LIBWEBSOCKETS_FOUND)

if(BUILD_LIBWEBSOCKETS)

  message(STATUS "Finding MBedTLS")
  find_mbedtls()
  if(NOT MBEDTLS_FOUND)
    build_mbedtls()
  endif(NOT MBEDTLS_FOUND)

  build_libwebsockets()
endif(BUILD_LIBWEBSOCKETS)

set(CMAKE_BUILD_RPATH "${MBEDTLS_LIBRARY_DIR};${CMAKE_CURRENT_BINARY_DIR}/curl/lib" CACHE PATH "Build with local rpath")
set(CMAKE_INSTALL_RPATH "${MBEDTLS_LIBRARY_DIR}" CACHE PATH "Build with install rpath")

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

  target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES} ${CURL_LIBRARY} mbedtls mbedx509 mbedcrypto dl)
  set_target_properties(${TARGET_NAME} PROPERTIES PREFIX "" OUTPUT_NAME "${NAME}" # COMPILE_FLAGS "-fvisibility=hidden"
                                                  BUILD_RPATH "${MBEDTLS_LIBRARY_DIR};${CMAKE_BINARY_DIR};${CMAKE_CURRENT_BINARY_DIR};${CMAKE_BINARY_DIR}/quickjs;${CMAKE_CURRENT_BINARY_DIR}/quickjs")
  target_compile_definitions(${TARGET_NAME} PRIVATE -DJS_${UNAME}_MODULE=1)
  #message("C module dir: ${QUICKJS_C_MODULE_DIR}")
  install(TARGETS ${TARGET_NAME} DESTINATION ${QUICKJS_C_MODULE_DIR})

  if(LIBWEBSOCKETS_FOUND)
    target_include_directories(${TARGET_NAME} PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS})
    target_link_libraries(${TARGET_NAME} ${LIBWEBSOCKETS_LIBRARIES})
  endif()
endfunction()

# if(NOT USE_SYSTEM_LIBWEBSOCKETS)
link_directories(${LIBWEBSOCKETS_LIBRARY_DIRS})
# endif(NOT USE_SYSTEM_LIBWEBSOCKETS)

file(GLOB JS_BINDINGS_SOURCES *.c *.h)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Main
add_library(quickjs-minnet MODULE ${JS_BINDINGS_SOURCES})
if(BUILD_LIBWEBSOCKETS)
  add_dependencies(quickjs-minnet libwebsockets)
endif(BUILD_LIBWEBSOCKETS)

target_include_directories(quickjs-minnet PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS} ${CURL_INCLUDE_DIR})
set_target_properties(quickjs-minnet PROPERTIES COMPILE_FLAGS "-fvisibility=hidden" PREFIX "" OUTPUT_NAME "net" # OUTPUT_NAME_DEBUG "net-debug"
)
target_compile_definitions(quickjs-minnet PRIVATE -DJS_SHARED_LIBRARY=1 -DJS_BINDINGS_INIT_MODULE=1)
target_link_libraries(quickjs-minnet ${LIBWEBSOCKETS_LIBRARIES} ${CURL_LIBRARY} mbedtls mbedx509 mbedcrypto ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})

#message("C module dir: ${QUICKJS_C_MODULE_DIR}")
install(TARGETS quickjs-minnet DESTINATION "${QUICKJS_C_MODULE_DIR}" PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ)

# link
