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

  includedirs {"lib"}

  externalincludedirs {
    "../quickjs",
    "../libwebsockets/include"
  }

  libdirs {
    "../quickjs",
    "../libwebsockets/lib"
  }

  links {
      "quickjs",
      "websockets",
      "ssl",
      "crypto"
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
