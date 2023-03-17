solution "minnet"
location("build")
configurations { "Debug", "Release" }
platforms { "native", "x64", "x32" }

  project "minnet"
  language "C"
  kind "SharedLib"

  files { 
    "lib/*.c",
    "src/*.c"
  }

  defines { "JS_SHARED_LIBRARY" }

  includedirs { 
      "lib",
      "/opt/libressl-3.5.1/include",
      "/opt/libwebsockets/include",
      "/usr/local/include/quickjs"
   }

  libdirs { 
      "/opt/libressl-3.5.1/lib",
      "/opt/libwebsockets/lib",
      "/usr/local/lib/x86_64-linux-gnu"
  }
  
  links { 
      "websockets",
      "brotlienc",
      "brotlidec",
      "brotlicommon",
      "ssl",
      "crypto",
      "z",
      "quickjs"
  }

  linkoptions { 
      "-Wl,-soname,net.so",
      "-Wl,-rpath=/opt/libressl-3.5.1/lib:/opt/libwebsockets/lib:/usr/local/lib/x86_64-linux-gnu"
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
