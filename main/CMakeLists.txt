set(SRC_FILES "src/main.c")

idf_component_register(SRCS ${SRC_FILES}
                    INCLUDE_DIRS "."
                    REQUIRES bsp amp ui sensor button utils)
