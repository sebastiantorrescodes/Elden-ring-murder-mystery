cmake_minimum_required(VERSION 3.27)
project(ProximityChatMod)

set(CMAKE_CXX_STANDARD 17)


add_library(ProximityChatMod SHARED dllmain.cpp
        framework.h
        GameVersion.cpp
        GameVersion.h
        pch.cpp
        pch.h
        PointerChain.h
        Signature.cpp
        Signature.h
        WorldChrMan.cpp
        WorldChrMan.h
        TcpTestClient.cpp
        TcpTestClient.h
)
target_link_libraries(ProximityChatMod ws2_32 version)
