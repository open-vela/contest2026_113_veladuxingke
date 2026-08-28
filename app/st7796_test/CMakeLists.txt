if(CONFIG_CONTEST2026_113_ST7796_TEST)
  nuttx_add_application(
    NAME st7796_test
    SRCS st7796_test_main.c
    STACKSIZE 4096)
endif()
