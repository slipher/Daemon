include(DaemonGame)

GAMEMODULE(NAME cgame
    FLAGS
        ${WARNINGS}
    FILES
        src/dummygame/cgame.cpp
)


message("dummygame CMAKE_CXX_FLAGS is... ${CMAKE_CXX_FLAGS}")
