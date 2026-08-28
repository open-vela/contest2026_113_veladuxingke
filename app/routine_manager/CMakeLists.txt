# Contest 2026 team 113 desktop routine manager

if(CONFIG_CONTEST2026_113_ROUTINE_MANAGER)
  set(SRCS routine_manager_main.c routine_model.c routine_ui.c routine_storage.c
      routine_config.c)

  if(CONFIG_ROUTINE_MANAGER_ENVMON)
    list(APPEND SRCS
      envmon_i2c.c
      envmon_sht40.c
      envmon_bh1750.c
      envmon_sgp30.c
      environment_service.c)
  endif()

  if(CONFIG_ROUTINE_MANAGER_VOICE)
    list(APPEND SRCS
      audio_service.c
      voice_recognizer_demo.c
      voice_reply.c)

    if(CONFIG_ROUTINE_MANAGER_KWS)
      list(APPEND SRCS
        voice_recognizer_kws.cc
        kws_frontend.cc
        kws_model_data.cc)
    endif()

    if(CONFIG_ROUTINE_MANAGER_KWS_CAPTURE)
      list(APPEND SRCS kws_capture.c)
    endif()
  endif()

  nuttx_add_application(
    NAME
    ${CONFIG_ROUTINE_MANAGER_PROGNAME}
    PRIORITY
    ${CONFIG_ROUTINE_MANAGER_PRIORITY}
    STACKSIZE
    ${CONFIG_ROUTINE_MANAGER_STACKSIZE}
    MODULE
    ${CONFIG_CONTEST2026_113_ROUTINE_MANAGER}
    SRCS
    ${SRCS})

  if(CONFIG_ROUTINE_MANAGER_KWS)
    target_include_directories(
      apps_${CONFIG_ROUTINE_MANAGER_PROGNAME}
      PRIVATE
      ${APPDIR}/mlearning/tflite-micro/tflite-micro
      ${APPDIR}/math/gemmlowp/gemmlowp
      ${APPDIR}/math/kissfft/kissfft
      ${APPDIR}/math/ruy/ruy
      ${APPDIR}/system/flatbuffers/flatbuffers/include)
    target_compile_options(
      apps_${CONFIG_ROUTINE_MANAGER_PROGNAME}
      PRIVATE
      -DTF_LITE_STATIC_MEMORY
      -DTF_LITE_DISABLE_X86_NEON
      -DTFLITE_WITH_STABLE_ABI=0
      -DTFLITE_USE_OPAQUE_DELEGATE=0
      -DTFLITE_SINGLE_ROUNDING=0)
    if(CONFIG_TFLITEMICRO_BENCHMARK_TOOL)
      target_compile_options(
        apps_${CONFIG_ROUTINE_MANAGER_PROGNAME}
        PRIVATE
        -DTF_LITE_USE_CTIME)
    else()
      target_compile_options(
        apps_${CONFIG_ROUTINE_MANAGER_PROGNAME}
        PRIVATE
        -DTF_LITE_STRIP_ERROR_STRINGS)
    endif()
  endif()
endif()
