macro(find_curl)
  include(FindCURL)
  include(FindPkgConfig)
  pkg_search_module(CURL libcurl)

  if(NOT CURL_FOUND)
    find_library(CURL NAMES libcurl curl PATHS "${CMAKE_INSTALL_PREFIX}/lib")
  endif(NOT CURL_FOUND)

  if(NOT CURL_LIBRARY)
    if(CURL_LIBRARY_DEBUG)
      set(CURL_LIBRARY "${CURL_LIBRARY_DEBUG}")
    endif(CURL_LIBRARY_DEBUG)
  endif(NOT CURL_LIBRARY)
  if(NOT CURL_LIBRARY)
    if(pkgcfg_lib_CURL_curl)
      set(CURL_LIBRARY "${pkgcfg_lib_CURL_curl}")
    endif(pkgcfg_lib_CURL_curl)
  endif(NOT CURL_LIBRARY)

  if(CURL_LIBRARY)
    set(CURL_LIBRARY "${CURL_LIBRARY}" CACHE PATH "curl library")
  endif(CURL_LIBRARY)

  if(NOT CURL_INCLUDE_DIR)
    string(REGEX REPLACE "/lib" "/include" CURL_INCLUDE_DIR "${CURL_LIBRARY_DIR}")

    if(CURL_INCLUDE_DIR)
      set(CURL_INCLUDE_DIR "${CURL_INCLUDE_DIR}" CACHE PATH "curl include directory")
    endif(CURL_INCLUDE_DIR)
  endif(NOT CURL_INCLUDE_DIR)

  if(NOT CURL_LIBRARY_DIR)
    if(EXISTS ${CURL_LIBRARY})
      get_filename_component(CURL_LIBRARY_DIR "${CURL_LIBRARY}" DIRECTORY)
    endif(EXISTS ${CURL_LIBRARY})

    if(CURL_LIBRARY_DIR)
      set(CURL_LIBRARY_DIR "${CURL_LIBRARY_DIR}" CACHE PATH "curl library directory")
    endif(CURL_LIBRARY_DIR)
  endif(NOT CURL_LIBRARY_DIR)

  include_directories(${CURL_INCLUDE_DIR})
endmacro(find_curl)
