# Contest 2026 team 113 Wi-Fi manager

if(CONFIG_CONTEST2026_113_WIFI_MANAGER)
  nuttx_add_application(
    NAME
    wifi_manager
    PRIORITY
    ${CONFIG_CONTEST2026_113_WIFI_MANAGER_PRIORITY}
    STACKSIZE
    ${CONFIG_CONTEST2026_113_WIFI_MANAGER_STACKSIZE}
    MODULE
    ${CONFIG_CONTEST2026_113_WIFI_MANAGER}
    SRCS
    wifi_manager_main.c
    wifi_config.c
    wifi_time_sync.c)
endif()
