solution "qjs-net"
location("build")
configurations { "Debug", "Release" }
platforms { "native", "x64", "x32" }

  project "qjs-net"
  language "C"
  kind "SharedLib"

  files { 
    "lib/*.c",
    "src/*.c"
  }

  defines { "JS_SHARED_LIBRARY" }

  includedirs { 
      "lib",
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
  targetdir(".")

  configuration "Debug"
      defines { "JS_SHARED_LIBRARY", "_DEBUG", "DEBUG_OUTPUT" }
      flags { "Symbols" }

  configuration "Release"
      defines { "JS_SHARED_LIBRARY", "NDEBUG" }
      flags { "Optimize" }
