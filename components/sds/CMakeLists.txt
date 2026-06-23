set(SRC_FILES "sds.c")

message(STATUS "sds version is 2.0.0")

idf_component_register(SRCS ${SRC_FILES}
        INCLUDE_DIRS ".")