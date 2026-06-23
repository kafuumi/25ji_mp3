set(SRC_FILES "src/main.c" "src/player.c")

idf_component_register(SRCS ${SRC_FILES}
                    INCLUDE_DIRS "."
                    REQUIRES bsp amp ui sensor)
