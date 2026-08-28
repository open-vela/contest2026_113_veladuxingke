############################################################################
# Contest 2026 team 113 Wi-Fi manager
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = wifi_manager
PRIORITY  = $(CONFIG_CONTEST2026_113_WIFI_MANAGER_PRIORITY)
STACKSIZE = $(CONFIG_CONTEST2026_113_WIFI_MANAGER_STACKSIZE)
MODULE    = $(CONFIG_CONTEST2026_113_WIFI_MANAGER)

MAINSRC = wifi_manager_main.c
CSRCS   = wifi_config.c wifi_time_sync.c

include $(APPDIR)/Application.mk
