set(SRC_FILES "src/main.c" "src/bench_sin.c")

idf_component_register(SRCS ${SRC_FILES}
                    INCLUDE_DIRS ".")
