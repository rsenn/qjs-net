cmake_minimum_required(VERSION 3.0)
project(qjs-net C)

message("Building project [1;31m${PROJECT_NAME}[0m")

set(DISABLE_WERROR TRUE CACHE BOOL "Disable -Werror flag")

#option(BUILD_SHARED_LIBWEBSOCKETS "Shared building of dependency libraries" OFF)
option(BUILD_LIBWEBSOCKETS "in-tree build of libwebsockets" ON)
option(DO_TESTS "Perform tests" ON)
option(USE_CURL "Use curl for fetch()" OFF)
if(USE_CURL)
  option(BUILD_CURL "Build curl library" ${LOCAL_CURL})
endif(USE_CURL)

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
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildCURL.cmake)
if(USE_CURL AND NOT BUILD_CURL)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindCURL.cmake)
endif(USE_CURL AND NOT BUILD_CURL)

if(NOT BUILD_LIBWEBSOCKETS)
  include(FindZLIB)
endif(NOT BUILD_LIBWEBSOCKETS)

find_quickjs()
configure_quickjs()

if(WIN32 OR MINGW)
  set(SOCKET_LIBRARIES mswsock ws2_32)
endif(WIN32 OR MINGW)

if(USE_CURL)
  if(BUILD_CURL)
    set(LOCAL_CURL ON)
  else(BUILD_CURL)
    set(LOCAL_CURL OFF)
  endif(BUILD_CURL)

  if(NOT BUILD_CURL)
    find_curl()
  endif(NOT BUILD_CURL)

  if(NOT CURL_FOUND OR BUILD_CURL)
    build_curl()
  endif()
  message(STATUS "CURL library: ${CURL_LIBRARY}")

  set(CURL_LIBRARY "${CURL_LIBRARY}" CACHE PATH "curl library")

  add_definitions(-DUSE_CURL)
endif(USE_CURL)

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

include(CheckTypeSize)

if(MSYS OR MINGW OR CYGWIN OR WIN32 OR WIN64)
  check_includes(winsock2.h)
  if(HAVE_WINSOCK2_H)
    set(CMAKE_EXTRA_INCLUDE_FILES winsock2.h)
    add_definitions(-DHAVE_WINSOCK2_H)
    check_type_size("struct pollfd" HAVE_STRUCT_POLLFD)
    #message("HAVE_STRUCT_POLLFD: ${HAVE_STRUCT_POLLFD}")
    check_type_size(WSAPOLLFD HAVE_WSAPOLLFD)
    #message("HAVE_WSAPOLLFD: ${HAVE_WSAPOLLFD}")
  endif()
endif()

if(NOT BUILD_LIBWEBSOCKETS)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindLibwebsockets.cmake)
  find_libwebsockets()
  include_directories(${LIBWEBSOCKETS_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})
  link_libraries(${LIBWEBSOCKETS_LIBRARIES} ${BROTLI_LIBRARIES} ${ZLIB_LIBRARY} ${CURL_LIBRARY} ${OPENSSL_LIBRARIES})
endif(NOT BUILD_LIBWEBSOCKETS)

if(NOT LIBWEBSOCKETS_FOUND)
  set(BUILD_LIBWEBSOCKETS ON CACHE BOOL "build 3rdparty libwebsockets")
endif(NOT LIBWEBSOCKETS_FOUND)

if(BUILD_LIBWEBSOCKETS)

  option(WITH_SSL "Use SSL" ON)
  option(WITH_MBEDTLS "Use MbedTLS replacement for OpenSSL" OFF)
  option(WITH_BROTLI "Use brotli HTTP stream compression" ON)

  set(LIBWEBSOCKETS_DEPS "")
  set(LIBWEBSOCKETS_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")

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

    if(OPENSSL_ROOT_DIR)
      list(APPEND CMAKE_PREFIX_PATH "${OPENSSL_ROOT_DIR}")
    endif(OPENSSL_ROOT_DIR)

    if(NOT OPENSSL_LIBRARIES)

      include(FindOpenSSL)
      #[[if(NOT PKG_CONFIG_FOUND)
        include(FindPkgConfig)
      endif(NOT PKG_CONFIG_FOUND)
      pkg_check_modules(OPENSSL openssl)
      #find_package(OpenSSL REQUIRED)
      #pkg_search_module(OPENSSL openssl)
]]
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

  include(FindZLIB)

  build_libwebsockets()

  if(TARGET libmbedtls)
    #ExternalProject_Add_StepDependencies(libwebsockets configure libmbedtls)
    #add_dependencies(libwebsockets libmbedtls)
  endif(TARGET libmbedtls)
  if(TARGET libbrotli)
    ExternalProject_Add_StepDependencies(libwebsockets configure libbrotli)
    #add_dependencies(libwebsockets libbrotli)
  endif(TARGET libbrotli)
else(BUILD_LIBWEBSOCKETS)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindBrotli.cmake)

  find_brotli()
  if(WIN32)
    link_libraries(crypt32 iphlpapi)
  endif(WIN32)
endif(BUILD_LIBWEBSOCKETS)

if(OPENSSL_INCLUDE_DIR AND NOT OPENSSL_LIBRARY_DIR)
  string(REGEX REPLACE "/include" "/lib" OPENSSL_LIBRARY_DIR "${OPENSSL_INCLUDE_DIR}")

  set(CMAKE_INSTALL_RPATH "${OPENSSL_LIBRARY_DIR}")
endif()

#if(OPENSSL_LIBRARIES AND NOT OPENSSL_LIBRARY_DIR)
if(OPENSSL_LIBRARIES MATCHES "/lib/.*")
  string(REGEX REPLACE "/lib/.*" "/lib" OPENSSL_LIBRARY_DIR "${OPENSSL_LIBRARIES}")
endif(OPENSSL_LIBRARIES MATCHES "/lib/.*")

set(OPENSSL_LIBRARY_DIR "${OPENSSL_LIBRARY_DIR}" CACHE PATH "OpenSSL library directory")
set(CMAKE_INSTALL_RPATH "${OPENSSL_LIBRARY_DIR}")
#endif()

check_functions(poll)

if(HAVE_POLL)
  add_definitions(-DHAVE_POLL)
endif(HAVE_POLL)

if(USE_CURL)
  link_directories(${CURL_LIBRARY_DIR})
  include_directories(${CURL_INCLUDE_DIR})
endif(USE_CURL)

link_directories(${QUICKJS_LIBRARY_DIR} ${LIBWEBSOCKETS_LIBRARY_DIR} ${OPENSSL_LIBRARY_DIR} ${MBEDTLS_LIBRARY_DIR})
include_directories(${QUICKJS_INCLUDE_DIR} ${LIBWEBSOCKETS_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR} ${MBEDTLS_INCLUDE_DIR})

file(GLOB JS_BINDINGS_SOURCES *.c *.h)
list(FILTER JS_BINDINGS_SOURCES EXCLUDE REGEX ".*-plugin-.*")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
string(ASCII 27 ESC)

function(DUMP_TARGET TARGET)
  foreach(PROP BINARY_DIR BUILD_RPATH COMPILE_DEFINITIONS COMPILE_FLAGS INCLUDE_DIRECTORIES INTERFACE_LINK_DIRECTORIES INTERFACE_LINK_LIBRARIES LINK_DIRECTORIES LINK_LIBRARIES
               MANUALLY_ADDED_DEPENDENCIES NAME OUTPUT_NAME PCH_WARN_INVALID POSITION_INDEPENDENT_CODE SOURCE_DIR SOURCES TYPE)
    get_target_property(VALUE "${TARGET}" "${PROP}")
    if(VALUE)
      message("${ESC}[1;33m${TARGET} ${ESC}[1;31m${PROP} = ${ESC}[1;36m'${VALUE}'${ESC}[0m")
    endif(VALUE)
  endforeach()
endfunction(DUMP_TARGET TARGET)

if(WIN32)
  add_library(quickjs-minnet MODULE ${JS_BINDINGS_SOURCES})
else(WIN32)
  add_library(quickjs-minnet SHARED ${JS_BINDINGS_SOURCES})
endif(WIN32)

if(BUILD_LIBWEBSOCKETS)
  add_dependencies(quickjs-minnet libwebsockets)
endif(BUILD_LIBWEBSOCKETS)

#set(CMAKE_SHARED_LINKER_FLAGS "-Wl,-rpath=${CMAKE_INSTALL_RPATH}:${CURL_LIBRARY_DIR}")

set(OPENSSL_LIBRARIES "${OPENSSL_SSL_LIBRARY};${OPENSSL_CRYPTO_LIBRARY}" CACHE STRING "OpenSSL libraries")

if(USE_CURL)
  target_include_directories(quickjs-minnet PRIVATE ${CURL_INCLUDE_DIR})
  target_link_libraries(quickjs-minnet ${CURL_LIBRARY})
endif(USE_CURL)

target_include_directories(quickjs-minnet PRIVATE ${LIBWEBSOCKETS_INCLUDE_DIR})
set_target_properties(
  quickjs-minnet PROPERTIES INSTALL_RPATH "${OPENSSL_LIBRARY_DIR}" COMPILE_FLAGS "-fvisibility=hidden" LINK_DIRECTORIES "${LIBWEBSOCKETS_LIBRARY_DIR};${OPENSSL_LIBRARY_DIR};${MBEDTLS_LIBRARY_DIR}"
                            INTERFACE_LINK_DIRECTORIES "${LIBWEBSOCKETS_LIBRARY_DIR};${OPENSSL_LIBRARY_DIR};${MBEDTLS_LIBRARY_DIR}" PREFIX "" OUTPUT_NAME "net")
target_compile_definitions(quickjs-minnet PRIVATE -DJS_SHARED_LIBRARY=1 -DJS_BINDINGS_INIT_MODULE=1)

if(EXISTS "${LIBWEBSOCKETS_LIBRARY_DIR}")
  target_link_directories(quickjs-minnet PUBLIC ${LIBWEBSOCKETS_LIBRARY_DIR})
endif(EXISTS "${LIBWEBSOCKETS_LIBRARY_DIR}")

target_link_directories(quickjs-minnet PUBLIC ${QUICKJS_LIBRARY_DIR} ${LIBWEBSOCKETS_LIBRARY_DIR})

target_link_libraries(quickjs-minnet ${QUICKJS_LIBRARY} ${LIBWEBSOCKETS_LIBRARIES} ${MBEDTLS_LIBRARIES} ${OPENSSL_LIBRARIES} ${SOCKET_LIBRARIES} ${BROTLI_LIBRARIES} ${ZLIB_LIBRARY})

message(
  "Library directories:\n\
  libwebsockets - ${LIBWEBSOCKETS_LIBRARY_DIR}\n\
  mbedtls - ${MBEDTLS_LIBRARY_DIR}\n\
  curl - ${CURL_LIBRARY_DIR}\n\
  openssl - ${OPENSSL_LIBRARY_DIR}\n\
")

message(
  "Include directories:\n\
  libwebsockets - ${LIBWEBSOCKETS_INCLUDE_DIR}\n\
  mbedtls - ${MBEDTLS_INCLUDE_DIR}\n\
  curl - ${CURL_INCLUDE_DIR}\n\
  openssl - ${OPENSSL_INCLUDE_DIR}\n\
")

message(
  "Libraries:\n\
  libwebsockets - ${LIBWEBSOCKETS_LIBRARIES}\n\
  mbedtls - ${MBEDTLS_LIBRARIES}\n\
  curl - ${CURL_LIBRARY}\n\
  openssl - ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY}\n\
")

if(NOT QUICKJS_C_MODULE_DIR)
  set(QUICKJS_C_MODULE_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/quickjs")
endif(NOT QUICKJS_C_MODULE_DIR)

install(TARGETS quickjs-minnet DESTINATION "${QUICKJS_C_MODULE_DIR}" PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_READ WORLD_EXECUTE)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/rpc.js DESTINATION "${QUICKJS_JS_MODULE_DIR}")
#dump_target(quickjs-minnet)

file(GLOB TESTS tests/test-*.js)

relative_path(TESTS "${CMAKE_CURRENT_SOURCE_DIR}" ${TESTS})

if(DO_TESTS)
  set(MODULE_PATH "${CMAKE_CURRENT_BINARY_DIR};${CMAKE_CURRENT_SOURCE_DIR}")
  set(ENV{QUICKJS_MODULE_PATH} "${MODULE_PATH}")

  include(CTest)

  foreach(TEST ${TESTS})
    basename(BASE "${TEST}")
    string(REPLACE "test_" "" NAME "${BASE}")
    add_test(NAME "${BASE}" COMMAND env "QUICKJS_MODULE_PATH=${MODULE_PATH}" "${QJS}" --bignum "${TEST}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

  endforeach(TEST ${TESTS})

  add_custom_target(do_tests COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_CURRENT_BINARY_DIR}" --target "test" COMMENT "Do all tests")
endif(DO_TESTS)

install(FILES wscli.js DESTINATION bin PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ)
