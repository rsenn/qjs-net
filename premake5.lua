location("build")

workspace "minnet"
configurations {"Debug", "Release"}

project "minnet"
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

includedirs {"lib"}

externalincludedirs {
    "/opt/openssl-3.0.2-x86_64/include",
    "/opt/libwebsockets-x86_64/include",
    "/usr/local/include/quickjs"
}

libdirs {
    "/opt/openssl-3.0.2-x86_64/lib",
    "/opt/libwebsockets-x86_64/lib",
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
    "-Wl,-soname,net.so"
}

runpathdirs {
    "/opt/openssl-3.0.2-x86_64/lib",
    "/opt/libwebsockets-x86_64/lib",
    "/usr/local/lib/x86_64-linux-gnu"
}

targetprefix ""
targetname "net"
targetdir(".")

visibility "Hidden"

filter "configurations:Debug"
defines {"JS_SHARED_LIBRARY", "_DEBUG", "DEBUG_OUTPUT"}
symbols "On"
warnings "Default"

filter "configurations:Release"
defines {"JS_SHARED_LIBRARY", "NDEBUG"}
optimize "On"
warnings "Off"
