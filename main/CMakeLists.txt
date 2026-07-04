set(SRC_FILES "src/main.c")

idf_component_register(SRCS ${SRC_FILES}
                    INCLUDE_DIRS "."
                    REQUIRES button bsp amp ui sensor utils)
