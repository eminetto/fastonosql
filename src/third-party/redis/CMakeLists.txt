CMAKE_MINIMUM_REQUIRED(VERSION 3.0.0)
PROJECT(hiredis)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_LIST_DIR}/deps/hiredis ${CMAKE_CURRENT_LIST_DIR}/src ../sds)

SET(HEADERS_HIREDIS
    src/crc64.h

    deps/hiredis/read.h
    deps/hiredis/fmacros.h
)

SET(SOURCES_HIREDIS
    src/crc64.c

    deps/hiredis/net.c
    deps/hiredis/read.c
    deps/hiredis/hiredis.c
)

ADD_LIBRARY(hiredis OBJECT ${HEADERS_HIREDIS} ${SOURCES_HIREDIS})

