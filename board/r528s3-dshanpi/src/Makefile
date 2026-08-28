############################################################################
#
#   Copyright (C) 2013 Gregory Nutt. All rights reserved.
#   Author: Gregory Nutt <gnutt@nuttx.org>
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name NuttX nor the names of its contributors may be
#    used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
# COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
# OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
# AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
# ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
############################################################################

include $(TOPDIR)/Make.defs
include $(TOPDIR)/../vendor/allwinnertech/Common.mk
VPATH += $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/micro_sd
DEPPATH += --dep-path $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/micro_sd
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/micro_sd

# Realtek WiFi driver
ifeq ($(CONFIG_IEEE80211_REALTEK_WIFI),y)
# Add mbedtls include paths for WiFi crypto
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../apps/crypto/mbedtls/include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../apps/crypto/mbedtls/mbedtls/include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../apps/include/crypto

VPATH += $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211
VPATH += $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/os/customer_rtos
VPATH += $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/os/os_dep
VPATH += $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/api/wifi
VPATH += $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/api/option
VPATH += $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/platform/rtwlan_bsp
VPATH += $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/platform/sdio
DEPPATH += --dep-path $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211
DEPPATH += --dep-path $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/os/customer_rtos
DEPPATH += --dep-path $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/os/os_dep
DEPPATH += --dep-path $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/api/wifi
DEPPATH += --dep-path $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/api/option
DEPPATH += --dep-path $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/platform/rtwlan_bsp
DEPPATH += --dep-path $(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/platform/sdio
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/api/wifi
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/api/option
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/os/customer_rtos
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/os/os_dep
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/os/os_dep/include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/platform/include
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/platform/rtwlan_bsp
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/platform/sdio
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/platform/sdio/core
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/boards/r528/drivers/realtek_ieee80211/platform/sdio/include
CSRCS += realtek_netdev.c realtek_driver.c realtek_sdio.c realtek_gpio.c
CSRCS += customer_rtos_service.c osdep_service.c wifi_conf.c wifi_ind.c
CSRCS += wifi_promisc.c wifi_util.c device_lock.c net_stack_intf.c
CSRCS += rtwlan_bsp.c wifi_io.c rtw_opt_crypto_ssl.c
CFLAGS += -DCCVWIFIFW_PATH=\"/resource/etc/wifi/FW_NIC_ACUT.bin\"
CFLAGS += -DWIFIFW_PATH=\"/resource/etc/wifi/FW_NIC_BCUT.bin\"
endif

CSRCS += r528_boot.c r528_leds.c
CSRCS += r528_appinit.c r528_bringup.c
CSRCS += shim_binary_compat.c

ifeq ($(CONFIG_MICRO_TF),y)
CSRCS += micro_sd_driver.c
endif

ifeq ($(CONFIG_INPUT_FT5X06),y)
CSRCS += r528_ft5x06.c
endif

ifeq ($(CONFIG_R528_ST7796),y)
CSRCS += r528_st7796.c r528_st7796_board.c
endif

ifeq ($(CONFIG_ETC_ROMFS),y)
  RCSRCS += etc/init.d/rc.sysinit etc/init.d/rcS
 # RCRAWS += etc/key.avb
  RCRAWS += etc/build.prop
  RCRAWS += etc/factory.sh
 # RCRAWS += etc/rsa_public_key.pem
ifeq ($(CONFIG_X4B_AP),y)
  RCRAWS += etc/EQ_AD51652.conf
  RCRAWS += etc/EQ_ACM.conf
  RCRAWS += etc/EQ_AD52058.conf
  RCRAWS += etc/usr/share/mico
  ifeq ($(CONFIG_MICO_DEFAULT_TTS_PACK_INTO_FIRMWARE),y)
    RCRAWS += etc/usr/share/sound/common
    RCRAWS += etc/usr/share/sound/vendor
  else
    TTS_BASE_DIR = etc/usr/share/sound
    TTS_COPY_DIR = ${TOPDIR}/../vendor/allwinnertech/lichee/board/common/data/res
    ${shell rm -rf ${TTS_COPY_DIR}/${TTS_BASE_DIR}}
    ${shell mkdir -p ${TTS_COPY_DIR}/${TTS_BASE_DIR}}
    ${shell cp -r ${TTS_BASE_DIR}/* ${TTS_COPY_DIR}/${TTS_BASE_DIR}}
  endif
endif
ifeq ($(CONFIG_X4B_FACTEST),y)
  RCRAWS += etc/EQ_AD51652.conf
  RCRAWS += etc/EQ_ACM.conf
  RCRAWS += etc/EQ_AD52058.conf
  RCRAWS += etc/wifi_factest/rtl8733bs_wifi.map
  RCRAWS += etc/wifi_factest/rtl8733bs_wifi.mask
endif
endif

ifeq ($(CONFIG_LIBC_LOCALTIME),y)
RCRAWS += etc/zoneinfo
endif

ifeq ($(CONFIG_SERVICES_MICO_ALARM),y)
RCRAWS += etc/workday
endif

ifeq ($(CONFIG_MIDESS),y)
RCRAWS += etc/mi/ui_support.json
endif

ifeq ($(CONFIG_MEDIA_SERVER),y)
RCRAWS += etc/media
endif

RCRAWS += etc/fonts

ifeq ($(CONFIG_ROUTINE_MANAGER_VOICE),y)
# Keep generation metadata and legacy prompts out of the size-constrained
# firmware.  test_voice_asset_contract.py locks this list to voice_reply.c.
ROUTINE_VOICE_RUNTIME := \
  etc/routine_voice/below_zero.pcm \
  etc/routine_voice/cue.pcm \
  etc/routine_voice/current_humidity_is.pcm \
  etc/routine_voice/current_light_is.pcm \
  etc/routine_voice/current_temperature_is.pcm \
  etc/routine_voice/current_time_is.pcm \
  etc/routine_voice/degree_celsius.pcm \
  etc/routine_voice/eco2_intro.pcm \
  etc/routine_voice/eight.pcm \
  etc/routine_voice/five.pcm \
  etc/routine_voice/four.pcm \
  etc/routine_voice/hour_unit.pcm \
  etc/routine_voice/hundred.pcm \
  etc/routine_voice/lux.pcm \
  etc/routine_voice/minute_unit.pcm \
  etc/routine_voice/nine.pcm \
  etc/routine_voice/one.pcm \
  etc/routine_voice/percentage.pcm \
  etc/routine_voice/point.pcm \
  etc/routine_voice/ppb.pcm \
  etc/routine_voice/ppm.pcm \
  etc/routine_voice/second_unit.pcm \
  etc/routine_voice/seven.pcm \
  etc/routine_voice/silence_80ms.pcm \
  etc/routine_voice/six.pcm \
  etc/routine_voice/temperature_high_warning.pcm \
  etc/routine_voice/temperature_low_warning.pcm \
  etc/routine_voice/ten.pcm \
  etc/routine_voice/ten_thousand.pcm \
  etc/routine_voice/thousand.pcm \
  etc/routine_voice/three.pcm \
  etc/routine_voice/today_goal_due.pcm \
  etc/routine_voice/tvoc_intro.pcm \
  etc/routine_voice/two.pcm \
  etc/routine_voice/zero.pcm
RCRAWS += $(ROUTINE_VOICE_RUNTIME)
endif

ifeq ($(CONFIG_COMPONENTS_BLUETOOTH),y)
ifeq ($(CONFIG_X4B_FACTEST),y)
  RCRAWS += etc/bt_factest
else
  RCRAWS += etc/bt
endif
endif

ifeq ($(CONFIG_SERVICES_PNS),y)
  PNS_BASE_DIR = ${TOPDIR}/../vendor/xiaomi/miai/pns/thirdparty/$(CONFIG_PNS_PRODUCT_SERIES)
  PNS_DST_DIR = etc/pns/configs/vpm/xiaomi
  ifneq ($(CONFIG_PNS_LIBVPM_ENABLE),)
    ${shell rm -rf etc/pns}
    ${shell mkdir -p ${PNS_DST_DIR}}
    ${shell cp -r ${PNS_BASE_DIR}/vpm/configs/xiaomi/* ${PNS_DST_DIR}}
    ifneq ($(CONFIG_PNS_CFG_PACK_INTO_FIRMWARE),)
      RCRAWS += ${PNS_DST_DIR}
    else
      PNS_COPY_DIR = ${TOPDIR}/../vendor/allwinnertech/lichee/board/common/data/res
      ${shell rm -rf ${PNS_COPY_DIR}/etc/pns}
      ${shell mkdir -p ${PNS_COPY_DIR}/${PNS_DST_DIR}}
      ifeq ($(wildcard etc/pns),)
        ${shell cp -r ${PNS_BASE_DIR}/vpm/configs/xiaomi/* ${PNS_COPY_DIR}/${PNS_DST_DIR}}
      else
        ${shell cp -r ${PNS_DST_DIR}/* ${PNS_COPY_DIR}/${PNS_DST_DIR}}
      endif
    endif
  endif
endif

ifeq ($(CONFIG_SYSTEM_PACKAGE_SERVICE), y)
RCRAWS += etc/package.cfg
endif

ifeq ($(CONFIG_SYSTEM_ACTIVITY_SERVICE), y)
RCRAWS += etc/lmk.cfg
endif

.PHONY: FORCE

FORCE:

ARCH_BOARD_CUSTOM_DIR := $(subst ",,${CONFIG_ARCH_BOARD_CUSTOM_DIR})
ARCH_BOARD_CUSTOM_NAME := $(subst ",,${CONFIG_ARCH_BOARD_CUSTOM_NAME})

ifeq ($(CONFIG_TA_ALIPAY),y)
ifneq ($(CONFIG_INTERPRETERS_WAMR),)
ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_alipay.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F1490647578C565BA2C321FD51DC6791: FORCE
	cp $(APPDIR)/bin/wasm/ta_alipay.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F1490647578C565BA2C321FD51DC6791
	cp $(APPDIR)/bin/wasm/ta_alipay.xip $(APPDIR)/staging/ta_alipay.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_alipay.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F1490647578C565BA2C321FD51DC6791: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_alipay.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F1490647578C565BA2C321FD51DC6791
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F1490647578C565BA2C321FD51DC6791: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F1490647578C565BA2C321FD51DC6791
endif
endif
  RCRAWS += etc/ta/F1490647578C565BA2C321FD51DC6791
endif
endif

ifeq ($(CONFIG_TA_COMSST),y)
ifneq ($(CONFIG_INTERPRETERS_WAMR),)
ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_comsst.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F617ACC99AE5405985D05056DF6BF28B: FORCE
	cp $(APPDIR)/bin/wasm/ta_comsst.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F617ACC99AE5405985D05056DF6BF28B
	cp $(APPDIR)/bin/wasm/ta_comsst.xip $(APPDIR)/staging/ta_comsst.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_comsst.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F617ACC99AE5405985D05056DF6BF28B: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_comsst.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F617ACC99AE5405985D05056DF6BF28B
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F617ACC99AE5405985D05056DF6BF28B: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/F617ACC99AE5405985D05056DF6BF28B
endif
endif
  RCRAWS += etc/ta/F617ACC99AE5405985D05056DF6BF28B
endif
endif

ifeq ($(CONFIG_TA_HELLO_WORLD),y)
ifneq ($(CONFIG_INTERPRETERS_WAMR),)
ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_hello_world.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/00F2AA8A5024E411ABE20002A5D5C51B: FORCE
	cp $(APPDIR)/bin/wasm/ta_hello_world.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/00F2AA8A5024E411ABE20002A5D5C51B
	cp $(APPDIR)/bin/wasm/ta_hello_world.xip $(APPDIR)/staging/ta_hello_world.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_hello_world.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/00F2AA8A5024E411ABE20002A5D5C51B: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_hello_world.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/00F2AA8A5024E411ABE20002A5D5C51B
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/00F2AA8A5024E411ABE20002A5D5C51B: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/00F2AA8A5024E411ABE20002A5D5C51B
endif
endif
  RCRAWS += etc/ta/00F2AA8A5024E411ABE20002A5D5C51B
endif
endif

ifeq ($(CONFIG_TA_TRIAD),y)
ifneq ($(CONFIG_INTERPRETERS_WAMR),)
ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_triad.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/1C6455C967DD255194BE7F9DCD557595: FORCE
	cp $(APPDIR)/bin/wasm/ta_triad.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/1C6455C967DD255194BE7F9DCD557595
	cp $(APPDIR)/bin/wasm/ta_triad.xip $(APPDIR)/staging/ta_triad.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_triad.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/1C6455C967DD255194BE7F9DCD557595: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_triad.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/1C6455C967DD255194BE7F9DCD557595
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/1C6455C967DD255194BE7F9DCD557595: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/1C6455C967DD255194BE7F9DCD557595
endif
endif
  RCRAWS += etc/ta/1C6455C967DD255194BE7F9DCD557595
endif
endif

ifeq ($(CONFIG_TA_WXCODEPAY),y)
ifneq ($(CONFIG_INTERPRETERS_WAMR),)
ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_wxcodepay.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/4EBA9EA8ACF42751AD93A5A4F03E1A67: FORCE
	cp $(APPDIR)/bin/wasm/ta_wxcodepay.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/4EBA9EA8ACF42751AD93A5A4F03E1A67
	cp $(APPDIR)/bin/wasm/ta_wxcodepay.xip $(APPDIR)/staging/ta_wxcodepay.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_wxcodepay.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/4EBA9EA8ACF42751AD93A5A4F03E1A67: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_wxcodepay.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/4EBA9EA8ACF42751AD93A5A4F03E1A67
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/4EBA9EA8ACF42751AD93A5A4F03E1A67: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/4EBA9EA8ACF42751AD93A5A4F03E1A67
endif
endif
  RCRAWS += etc/ta/4EBA9EA8ACF42751AD93A5A4F03E1A67
endif
endif

ifeq ($(CONFIG_TA_PIN),y)
ifneq ($(CONFIG_INTERPRETERS_WAMR),)
ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_pin.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/EE571882BE621355ADAB1B7F23DEFAA6: FORCE
	cp $(APPDIR)/bin/wasm/ta_pin.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/EE571882BE621355ADAB1B7F23DEFAA6
	cp $(APPDIR)/bin/wasm/ta_pin.xip $(APPDIR)/staging/ta_pin.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_pin.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/EE571882BE621355ADAB1B7F23DEFAA6: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_pin.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/EE571882BE621355ADAB1B7F23DEFAA6
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/EE571882BE621355ADAB1B7F23DEFAA6: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/EE571882BE621355ADAB1B7F23DEFAA6
endif
endif
  RCRAWS += etc/ta/EE571882BE621355ADAB1B7F23DEFAA6
endif
endif

ifeq ($(CONFIG_TA_KEYSTORE),y)
ifneq ($(CONFIG_INTERPRETERS_WAMR),)
ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_keystore.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/88A89F14B906EF4EBD96940EFFDABEF4: FORCE
	cp $(APPDIR)/bin/wasm/ta_keystore.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/88A89F14B906EF4EBD96940EFFDABEF4
	cp $(APPDIR)/bin/wasm/ta_keystore.xip $(APPDIR)/staging/ta_keystore.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_keystore.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/88A89F14B906EF4EBD96940EFFDABEF4: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_keystore.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/88A89F14B906EF4EBD96940EFFDABEF4
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/88A89F14B906EF4EBD96940EFFDABEF4: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/88A89F14B906EF4EBD96940EFFDABEF4
endif
endif
  RCRAWS += etc/ta/88A89F14B906EF4EBD96940EFFDABEF4
endif
endif

ifeq ($(CONFIG_OPTEE_TEST_TA),y)
ifneq ($(CONFIG_INTERPRETERS_WAMR),)
ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_crypt.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A05B3ECBF1ADE011998B0002A5D5C51B: FORCE
	cp $(APPDIR)/bin/wasm/ta_crypt.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A05B3ECBF1ADE011998B0002A5D5C51B
	cp $(APPDIR)/bin/wasm/ta_crypt.xip $(APPDIR)/staging/ta_crypt.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_crypt.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A05B3ECBF1ADE011998B0002A5D5C51B: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_crypt.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A05B3ECBF1ADE011998B0002A5D5C51B
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A05B3ECBF1ADE011998B0002A5D5C51B: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A05B3ECBF1ADE011998B0002A5D5C51B
endif
endif
  RCRAWS += etc/ta/A05B3ECBF1ADE011998B0002A5D5C51B

ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_storage.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A7F289B6DF8A7A479F9932E90C0AD0A2: FORCE
	cp $(APPDIR)/bin/wasm/ta_storage.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A7F289B6DF8A7A479F9932E90C0AD0A2
	cp $(APPDIR)/bin/wasm/ta_storage.xip $(APPDIR)/staging/ta_storage.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_storage.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A7F289B6DF8A7A479F9932E90C0AD0A2: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_storage.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A7F289B6DF8A7A479F9932E90C0AD0A2
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A7F289B6DF8A7A479F9932E90C0AD0A2: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/A7F289B6DF8A7A479F9932E90C0AD0A2
endif
endif
  RCRAWS += etc/ta/A7F289B6DF8A7A479F9932E90C0AD0A2

ifneq ($(wildcard $(APPDIR)/bin/wasm/ta_storage2.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/9E271E73FBAA7545A77138CAA6F0CCA6: FORCE
	cp $(APPDIR)/bin/wasm/ta_storage2.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/9E271E73FBAA7545A77138CAA6F0CCA6
	cp $(APPDIR)/bin/wasm/ta_storage2.xip $(APPDIR)/staging/ta_storage2.xip
else
ifneq ($(wildcard $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_storage2.xip),)
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/9E271E73FBAA7545A77138CAA6F0CCA6: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	cp $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/libs/$(ARCH_BOARD_CUSTOM_NAME)/ta_storage2.xip $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/9E271E73FBAA7545A77138CAA6F0CCA6
else
$(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/9E271E73FBAA7545A77138CAA6F0CCA6: FORCE
	mkdir -p $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta
	touch $(TOPDIR)/$(ARCH_BOARD_CUSTOM_DIR)/src/etc/ta/9E271E73FBAA7545A77138CAA6F0CCA6
endif
endif
  RCRAWS += etc/ta/9E271E73FBAA7545A77138CAA6F0CCA6

endif
endif


include $(TOPDIR)/boards/Board.mk

AW_SRCDIR = $(TOPDIR)/arch/arm/src/r528
CFLAGS += ${INCDIR_PREFIX}$(AW_SRCDIR)
CFLAGS += ${INCDIR_PREFIX}$(AW_SRCDIR)$(DELIM)include$(DELIM)drivers

#include $(APPDIR)/Make.defs

CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/vendor/allwinnertech/chips/r528/drivers/rtos-hal/include/osal
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)/vendor/allwinnertech/chips/r528/components/secure_storage
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/chips/r528/drivers/rtos-hal/hal/source/hwspinlock
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/chips/r528/drivers/rtos-hal/include/hal
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/chips/r528/drivers/rtos-hal/include/osal
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/allwinnertech/chips/r528/drivers/rtos-hal/hal/source

RCRAWS += etc/lvx_video_config.json

ifeq ($(CONFIG_BLUETOOTH),y)
RCRAWS += etc/bt/8723fs_btaddr.txt
RCRAWS += etc/bt/8723fs_config_C-cut.bin
RCRAWS += etc/bt/8723fs_fw_C-cut.bin
endif

ifeq ($(CONFIG_UTILS_SSH),y)
RCRAWS += etc/ssh/ssh_config
RCRAWS += etc/ssh/ssh_known_hosts
endif
