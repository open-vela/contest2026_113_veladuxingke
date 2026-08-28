# Contest 2026 team 113 LAN panel

include $(APPDIR)/Make.defs

ifneq ($(CONFIG_CONTEST2026_113_LAN_PANEL),)
PROGNAME  = lan_panel
PRIORITY  = $(CONFIG_CONTEST2026_113_LAN_PANEL_PRIORITY)
STACKSIZE = $(CONFIG_CONTEST2026_113_LAN_PANEL_STACKSIZE)
MODULE    = $(CONFIG_CONTEST2026_113_LAN_PANEL)

MAINSRC = lan_panel_main.c
CSRCS   = lan_http.c lan_auth.c wifi_config.c

include $(APPDIR)/Application.mk
endif
