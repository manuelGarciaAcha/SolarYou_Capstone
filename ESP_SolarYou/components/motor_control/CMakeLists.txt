idf_component_register(
    SRCS "motor_control.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        board_config
        espressif__servo
)