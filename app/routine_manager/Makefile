############################################################################
# Contest 2026 team 113 desktop routine manager
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_ROUTINE_MANAGER_PROGNAME)
PRIORITY  = $(CONFIG_ROUTINE_MANAGER_PRIORITY)
STACKSIZE = $(CONFIG_ROUTINE_MANAGER_STACKSIZE)
MODULE    = $(CONFIG_CONTEST2026_113_ROUTINE_MANAGER)

MAINSRC = routine_manager_main.c
CSRCS   = routine_model.c routine_ui.c routine_storage.c routine_config.c
CSRCS  += wifi_ui.c wifi_status_monitor.c

ifeq ($(CONFIG_ROUTINE_MANAGER_ENVMON),y)
CSRCS  += envmon_i2c.c envmon_sht40.c envmon_bh1750.c envmon_sgp30.c
CSRCS  += environment_service.c
endif

ifeq ($(CONFIG_ROUTINE_MANAGER_VOICE),y)
CSRCS  += audio_service.c voice_recognizer_demo.c voice_reply.c
ifeq ($(CONFIG_ROUTINE_MANAGER_KWS),y)
CXXEXT  = .cc
CXXSRCS += voice_recognizer_kws.cc kws_frontend.cc kws_model_data.cc
TFLM_DIR = $(APPDIR)/mlearning/tflite-micro/tflite-micro
CXXFLAGS += ${INCDIR_PREFIX}$(TFLM_DIR)
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/gemmlowp/gemmlowp
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/kissfft/kissfft
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/ruy/ruy
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/system/flatbuffers/flatbuffers/include
CXXFLAGS += -DTFLITE_EMULATE_FLOAT
CXXFLAGS += -DTF_LITE_DISABLE_X86_NEON
CXXFLAGS += -DTFLITE_WITH_STABLE_ABI=0
CXXFLAGS += -DTFLITE_USE_OPAQUE_DELEGATE=0
CXXFLAGS += -DTFLITE_SINGLE_ROUNDING=0
ifneq ($(CONFIG_TFLITEMICRO_DEBUG),)
CXXFLAGS += -DTFLITE_SHOW_MEMORY_USE
CXXFLAGS += -DTF_LITE_USE_CTIME
else
CXXFLAGS += -DTF_LITE_STRIP_ERROR_STRINGS
endif
endif
ifeq ($(CONFIG_ROUTINE_MANAGER_KWS_CAPTURE),y)
CSRCS  += kws_capture.c
endif
endif

include $(APPDIR)/Application.mk
