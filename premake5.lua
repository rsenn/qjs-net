location("build")

workspace "qjs-net"
  configurations {"Debug", "Release"}

project "qjs-net"
  language "C"
  kind "SharedLib"

  files {
      "lib/*.c",
      "src/*.c"
  }

  defines {"JS_SHARED_LIBRARY"}
  functionlevellinking "on"
  omitframepointer "on"
  pic "On"
  visibility "Hidden"

  includedirs { "lib" }

  externalincludedirs {
    "..",
    "../quickjs",
    "/usr/local/include/quickjs",
    "libwebsockets/include",
    "libwebsockets/build/x86_64-linux-gnu"

  }

  libdirs {
    "..",
    "../quickjs",
    "libwebsockets/lib",
    "libwebsockets/build/x86_64-linux-gnu/lib"
  }

  links {
    "quickjs",
    "websockets",
    "ssl","crypto",
    "brotlienc", "brotlidec", "brotlicommon", "z"
  }

  targetprefix ""
  targetname "net"

  filter "configurations:Debug"
    defines {"JS_SHARED_LIBRARY", "_DEBUG", "DEBUG_OUTPUT"}
    symbols "On"
    warnings "Default"

  filter "configurations:Release"
    defines {"JS_SHARED_LIBRARY", "NDEBUG"}
    optimize "On"
    warnings "Off"
