cmake_minimum_required(VERSION 3.0)
project(minnet C)

set(DISABLE_WERROR TRUE CACHE BOOL "Disable -Werror flag")

#option(BUILD_SHARED_LIBWEBSOCKETS "Shared building of dependency libraries" OFF)
option(BUILD_LIBWEBSOCKETS "in-tree build of libwebsockets" ON)
option(BUILD_CURL "Build curl library" ${LOCAL_CURL})

#set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "PIC code")
#set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")

if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
elseif(NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "" CACHE PATH "Specify the output directory for libraries (default is lib)")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UseMultiArch.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)

find_quickjs()
configure_quickjs()

if(WIN32 OR MINGW)
  set(SOCKET_LIBRARIES mswsock ws2_32)
endif(WIN32 OR MINGW)

#if(BUILD_SHARED_LIBWEBSOCKETS)
#  set(LWS_WITH_SHARED TRUE)
#  set(LWS_WITH_STATIC FALSE)
#else(BUILD_SHARED_LIBWEBSOCKETS)
#  set(LWS_WITH_SHARED FALSE)
#  set(LWS_WITH_STATIC TRUE)
#endif(BUILD_SHARED_LIBWEBSOCKETS)
#
#set(LWS_WITH_LIBUV OFF)
#set(LWS_STATIC_PIC TRUE)
#set(LWS_WITH_EXTERNAL_POLL ON)
#set(LWS_WITH_HTTP_BROTLI ON)
#set(LWS_WITH_HTTP_PROXY ON)
#set(LWS_WITH_HTTP_STREAM_COMPRESSION ON)
#set(LWS_WITH_PLUGINS OFF)
#set(LWS_WITH_PLUGINS_API OFF)
#set(LWS_WITH_PLUGINS_BUILTIN OFF)
#set(LWS_WITH_SOCKS5 ON)
#set(LWS_WITH_ZLIB ON)
#set(LWS_WITH_ZIP_FOPS ON)
#set(LWS_HAVE_LIBCAP FALSE)

if(BUILD_CURL)
  set(LOCAL_CURL ON)
else(BUILD_CURL)
  set(LOCAL_CURL OFF)
endif(BUILD_CURL)

#message("BUILD_CURL: ${BUILD_CURL}")

if(NOT BUILD_CURL)
  include(FindCURL)
  include(FindPkgConfig)
  pkg_search_module(CURL libcurl)

  if(NOT CURL_FOUND)
    find_library(CURL NAMES libcurl curl PATHS "${CMAKE_INSTALL_PREFIX}/lib")
  endif(NOT CURL_FOUND)

  if(NOT CURL_LIBRARY AND CURL_LIBRARY_DEBUG)
    set(CURL_LIBRARY "${CURL_LIBRARY_DEBUG}")
  endif(NOT CURL_LIBRARY AND CURL_LIBRARY_DEBUG)

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
      curl
      SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/curl"
      GIT_REPOSITORY "${CURL_REPO}"
      GIT_SUBMODULES_RECURSE TRUE
      GIT_PROGRESS TRUE
      BINARY_DIR curl
      STEP_TARGETS build
      CONFIGURE_COMMAND cmake "${CMAKE_CURRENT_SOURCE_DIR}/curl" -DLIBCURL_NO_SHARED:STRING=libcurl
                        -DBUILD_SHARED_LIBS:BOOL=OFF
      CMAKE_ARGS -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
      CMAKE_CACHE_ARGS -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF)
    set_property(DIRECTORY PROPERTY EP_STEP_TARGETS build)
  endif()

  if(CURL_LIBRARIES AND NOT CURL_LIBRARY)
    #set(CURL_LIBRARY "${CURL_LIBRARIES}")
  else(LIBCURL_NO_SHARED AND NOT CURL_LIBRARY)
    set(CURL_LIBRARY curl)
  endif()
  set(CURL_LIBRARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/curl/lib")
  set(CURL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/curl/include")
  link_directories(${CMAKE_CURRENT_BINARY_DIR}/curl/lib)
endif()
message(STATUS "CURL library: ${CURL_LIBRARY}")

set(CURL_LIBRARY "${CURL_LIBRARY}" CACHE PATH "curl library")

#set(DISABLE_WERROR ON)
#if(NOT QUICKJS_INCLUDE_DIR)
#  if(QUICKJS_INCLUDE_DIR)
#    set(QUICKJS_INCLUDE_DIR "${QUICKJS_INCLUDE_DIR}")
#  else(QUICKJS_INCLUDE_DIR)
#    set(QUICKJS_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
#
#  endif(QUICKJS_INCLUDE_DIR)
#endif(NOT QUICKJS_INCLUDE_DIR)
#
#if(EXISTS "${QUICKJS_INCLUDE_DIR}/quickjs")
#  set(QUICKJS_INCLUDE_DIR "${QUICKJS_INCLUDE_DIR}/quickjs")
#endif(EXISTS "${QUICKJS_INCLUDE_DIR}/quickjs")

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

if(NOT BUILD_LIBWEBSOCKETS)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindLibwebsockets.cmake)

  find_libwebsockets()

  include_directories(${LIBWEBSOCKETS_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIRS})

  link_libraries(${LIBWEBSOCKETS_LIBRARIES} ${OPENSSL_LIBRARIES})

endif(NOT BUILD_LIBWEBSOCKETS)

if(NOT LIBWEBSOCKETS_FOUND)
  set(BUILD_LIBWEBSOCKETS ON CACHE BOOL "build 3rdparty libwebsockets")
endif(NOT LIBWEBSOCKETS_FOUND)

if(BUILD_LIBWEBSOCKETS)

  option(WITH_SSL "Use SSL" ON)
  option(WITH_MBEDTLS "Use MbedTLS replacement for OpenSSL" OFF)
  option(WITH_BROTLI "Use brotli HTTP stream compression" ON)

  set(LIBWEBSOCKETS_DEPS "")
  set(LIBWEBSOCKETS_C_FLAGS "-D_GNU_SOURCE")

  if(WITH_MBEDTLS)
    set(WITH_SSL ON)

    option(BUILD_MBEDTLS "in-tree build of MbedTLS" OFF)

    if(NOT BUILD_MBEDTLS)
      include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindMbedTLS.cmake)

      find_mbedtls()
    endif(NOT BUILD_MBEDTLS)

    if(NOT MBEDTLS_FOUND)
      set(BUILD_MBEDTLS ON)
    endif(NOT MBEDTLS_FOUND)

    if(BUILD_MBEDTLS)
      include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildMbedTLS.cmake)

      set(MBEDTLS_DEBUG OFF)
      set(MBEDTLS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls")
      set(MBEDTLS_C_FLAGS "${CMAKE_C_FLAGS}")
      set(MBEDTLS_TARGET_NAME libmbedtls)

      build_mbedtls()

      list(APPEND LIBWEBSOCKETS_DEPS ${MBEDTLS_TARGET_NAME})
      set(LIBWEBSOCKETS_C_FLAGS "-I${MBEDTLS_INCLUDE_DIR} ${LIBWEBSOCKETS_C_FLAGS}")
    endif(BUILD_MBEDTLS)
  endif(WITH_MBEDTLS)
  if(WITH_SSL AND NOT WITH_MBEDTLS)
    if(NOT OPENSSL_LIBRARIES)
      include(FindPkgConfig)
      pkg_check_modules(OPENSSL openssl)
      find_package(OpenSSL REQUIRED)
      #pkg_search_module(OPENSSL openssl)

    endif(NOT OPENSSL_LIBRARIES)
  endif(WITH_SSL AND NOT WITH_MBEDTLS)

  if(WITH_BROTLI)
    option(BUILD_BROTLI "in-tree build of brotli" OFF)

    if(NOT BUILD_BROTLI)
      include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindBrotli.cmake)

      find_brotli()
    endif(NOT BUILD_BROTLI)

    if(NOT BROTLI_FOUND)
      set(BUILD_BROTLI ON)
    endif(NOT BROTLI_FOUND)

    if(BUILD_BROTLI)
      include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildBrotli.cmake)

      set(BROTLI_DEBUG OFF)
      set(BROTLI_C_FLAGS "${CMAKE_C_FLAGS}")
      set(BROTLI_TARGET_NAME libbrotli)

      build_brotli()
      list(APPEND LIBWEBSOCKETS_DEPS ${BROTLI_TARGET_NAME})
      set(LIBWEBSOCKETS_C_FLAGS "-I${BROTLI_INCLUDE_DIR} ${LIBWEBSOCKETS_C_FLAGS}")
    endif(BUILD_BROTLI)
  endif(WITH_BROTLI)

  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildLibwebsockets.cmake)

  build_libwebsockets()

  if(TARGET libmbedtls)
    #ExternalProject_Add_StepDependencies(libwebsockets configure libmbedtls)
    #add_dependencies(libwebsockets libmbedtls)
  endif(TARGET libmbedtls)
  if(TARGET libbrotli)
    ExternalProject_Add_StepDependencies(libwebsockets configure libbrotli)
    #add_dependencies(libwebsockets libbrotli)
  endif(TARGET libbrotli)

endif(BUILD_LIBWEBSOCKETS)

if(OPENSSL_INCLUDE_DIR AND NOT OPENSSL_LIBRARY_DIR)
  string(REGEX REPLACE "/include" "/lib" OPENSSL_LIBRARY_DIR "${OPENSSL_INCLUDE_DIR}")

  set(CMAKE_INSTALL_RPATH "${OPENSSL_LIBRARY_DIR}")
endif()

link_directories(${QUICKJS_LIBRARY_DIR} ${LIBWEBSOCKETS_LIBRARY_DIR} ${CURL_LIBRARY_DIR} ${OPENSSL_LIBRARY_DIR}
                 ${MBEDTLS_LIBRARY_DIR})
include_directories(${QUICKJS_INCLUDE_DIR} ${LIBWEBSOCKETS_INCLUDE_DIRS} ${CURL_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR}
                    ${MBEDTLS_INCLUDE_DIR})

file(GLOB JS_BINDINGS_SOURCES *.c *.h)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
string(ASCII 27 ESC)

function(DUMP_TARGET TARGET)
  foreach(
    PROP
    BINARY_DIR BUILD_RPATH COMPILE_DEFINITIONS COMPILE_FLAGS INCLUDE_DIRECTORIES INTERFACE_LINK_DIRECTORIES
    INTERFACE_LINK_LIBRARIES LINK_DIRECTORIES LINK_LIBRARIES MANUALLY_ADDED_DEPENDENCIES NAME OUTPUT_NAME
    PCH_WARN_INVALID POSITION_INDEPENDENT_CODE SOURCE_DIR SOURCES TYPE)
    get_target_property(VALUE "${TARGET}" "${PROP}")
    if(VALUE)
      message("${ESC}[1;33m${TARGET} ${ESC}[1;31m${PROP} = ${ESC}[1;36m'${VALUE}'${ESC}[0m")
    endif(VALUE)
  endforeach()
endfunction(DUMP_TARGET TARGET)

add_library(quickjs-minnet MODULE ${JS_BINDINGS_SOURCES})

if(BUILD_LIBWEBSOCKETS)
  add_dependencies(quickjs-minnet libwebsockets)
endif(BUILD_LIBWEBSOCKETS)

set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-rpath=${CMAKE_INSTALL_RPATH}")

set(OPENSSL_LIBRARIES "${OPENSSL_SSL_LIBRARY};${OPENSSL_CRYPTO_LIBRARY}" CACHE STRING "OpenSSL libraries")

target_include_directories(quickjs-minnet PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIRS} ${CURL_INCLUDE_DIR})
set_target_properties(
  quickjs-minnet
  PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_RPATH};${MBEDTLS_LIBRARY_DIR}"
             COMPILE_FLAGS "-fvisibility=hidden"
             LINK_DIRECTORIES "${OPENSSL_LIBRARY_DIR};${MBEDTLS_LIBRARY_DIR}"
             INTERFACE_LINK_DIRECTORIES "${MBEDTLS_LIBRARY_DIR}"
             PREFIX ""
             OUTPUT_NAME "net")
target_compile_definitions(quickjs-minnet PRIVATE -DJS_SHARED_LIBRARY=1 -DJS_BINDINGS_INIT_MODULE=1)

if(EXISTS "${LIBWEBSOCKETS_LIBRARY_DIR}")
  target_link_directories(quickjs-minnet PUBLIC ${LIBWEBSOCKETS_LIBRARY_DIR})
endif(EXISTS "${LIBWEBSOCKETS_LIBRARY_DIR}")

target_link_directories(quickjs-minnet PUBLIC ${QUICKJS_LIBRARY_DIR})

target_link_libraries(quickjs-minnet ${QUICKJS_LIBRARY} ${LIBWEBSOCKETS_LIBRARIES} ${MBEDTLS_LIBRARIES} ${CURL_LIBRARY}
                      ${OPENSSL_LIBRARIES} ${SOCKET_LIBRARIES})

message(
  "Libraries:\n\
  libwebsockets - ${LIBWEBSOCKETS_LIBRARIES}\n\
  mbedtls - ${MBEDTLS_LIBRARIES}\n\
  curl - ${CURL_LIBRARY}\n\
  openssl - ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY}\n\
")

message(
  "Library directories:\n\
  libwebsockets - ${LIBWEBSOCKETS_LIBRARY_DIR}\n\
  mbedtls - ${MBEDTLS_LIBRARY_DIR}\n\
  curl - ${CURL_LIBRARY_DIR}\n\
  openssl - ${OPENSSL_LIBRARY_DIR}\n\
")

if(NOT QUICKJS_C_MODULE_DIR)
  set(QUICKJS_C_MODULE_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/quickjs")
endif(NOT QUICKJS_C_MODULE_DIR)

install(TARGETS quickjs-minnet DESTINATION "${QUICKJS_C_MODULE_DIR}" PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                                                                                 GROUP_EXECUTE GROUP_READ)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rpc.js DESTINATION "${QUICKJS_JS_MODULE_DIR}")
#dump_target(quickjs-minnet)
