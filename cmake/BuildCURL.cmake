macro(build_curl)
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
      curl SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/curl" GIT_REPOSITORY "${CURL_REPO}" GIT_SUBMODULES_RECURSE TRUE GIT_PROGRESS TRUE BINARY_DIR curl STEP_TARGETS build
      CONFIGURE_COMMAND cmake "${CMAKE_CURRENT_SOURCE_DIR}/curl" -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF
      CMAKE_ARGS -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON CMAKE_CACHE_ARGS -DLIBCURL_NO_SHARED:STRING=libcurl -DBUILD_SHARED_LIBS:BOOL=OFF)
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
endmacro(build_curl)
