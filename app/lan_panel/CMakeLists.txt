# Contest 2026 team 113 LAN panel

if(CONFIG_CONTEST2026_113_LAN_PANEL)
  nuttx_add_application(
    NAME
    lan_panel
    PRIORITY
    ${CONFIG_CONTEST2026_113_LAN_PANEL_PRIORITY}
    STACKSIZE
    ${CONFIG_CONTEST2026_113_LAN_PANEL_STACKSIZE}
    MODULE
    ${CONFIG_CONTEST2026_113_LAN_PANEL}
    SRCS
    lan_panel_main.c
    lan_http.c
    lan_auth.c
    wifi_config.c)
endif()
