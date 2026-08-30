#!/usr/bin/env python3
"""Static contract checks for the Vela ai_agent product integration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
OPENVELA = ROOT.parent
AI_AGENT = OPENVELA / "packages" / "ai_agent"


class AiAgentContractTests(unittest.TestCase):
    def test_board_build_enables_runtime_and_persistent_directory(self) -> None:
        config = (ROOT / "board/r528s3-dshanpi/configs/nsh/defconfig").read_text()
        self.assertIn("CONFIG_EXAMPLES_AI_AGENT_VELA=y", config)
        self.assertIn('CONFIG_EXAMPLES_AI_AGENT_VELA_DATA_DIR="/data/agent"', config)

    def test_mimo_preset_is_available_for_product_backend(self) -> None:
        source = (AI_AGENT / "src/channels/cmd_llm.c").read_text()
        config = (AI_AGENT / "include/agent_config.h").read_text()
        self.assertIn('AGENT_LLM_MIMO_HOST, AGENT_LLM_MIMO_PATH', source)
        self.assertIn('AGENT_LLM_MIMO_MODEL "mimo-v2.5"', config)
        self.assertNotIn("mimo-v2-flash", source + config)
        self.assertNotIn("mimo-v2-omni", source + config)

    def test_mimo_v25_request_contract_matches_official_api(self) -> None:
        config = (AI_AGENT / "include/agent_config.h").read_text()
        proxy = (AI_AGENT / "src/llm/llm_proxy.c").read_text()
        for setting in (
            "AGENT_LLM_MIMO_MAX_TOKENS 32768",
            "AGENT_LLM_MIMO_TEMPERATURE 1.0",
            "AGENT_LLM_MIMO_TOP_P 0.95",
            'AGENT_LLM_MIMO_THINKING "disabled"',
        ):
            self.assertIn(setting, config)
        self.assertIn('cJSON_AddNumberToObject(body, "temperature"', proxy)
        self.assertIn('cJSON_AddNumberToObject(body, "top_p"', proxy)
        self.assertIn('cJSON_AddObjectToObject(body, "thinking")', proxy)
        self.assertIn('mimo_auth ? "api-key" : "Authorization"', proxy)
        self.assertIn('"api-key: %s\\r\\n"', proxy)

    def test_token_plan_key_selects_dedicated_mimo_endpoint(self) -> None:
        config = (AI_AGENT / "include/agent_config.h").read_text()
        proxy = (AI_AGENT / "src/llm/llm_proxy.c").read_text()
        command = (AI_AGENT / "src/channels/cmd_llm.c").read_text()
        router = (AI_AGENT / "src/llm/llm_router.c").read_text()
        self.assertIn(
            'AGENT_LLM_MIMO_TOKEN_PLAN_HOST "token-plan-cn.xiaomimimo.com"',
            config,
        )
        self.assertIn('strncmp(api_key, "tp-", 3) == 0', proxy)
        self.assertIn("migrate_host_config(s_llm_host", proxy)
        self.assertIn("llm_normalize_host_for_key(\n        backend.host", command)
        self.assertIn("llm_normalize_host_for_key(\n            b->host", router)

    def test_websocket_accepts_plain_text_frames(self) -> None:
        ws = (AI_AGENT / "src/channels/ws_server.c").read_text()
        self.assertIn("accept a JSON string or a raw text frame", ws)
        self.assertIn("content = frame_buf", ws)
        self.assertIn("ws_dispatch_chat(fd, chat_id, content", ws)

    def test_retired_mimo_models_migrate_in_all_persistent_configs(self) -> None:
        proxy = (AI_AGENT / "src/llm/llm_proxy.c").read_text()
        router = (AI_AGENT / "src/llm/llm_router.c").read_text()
        for old_model in (
            "mimo-v2-flash",
            "mimo-v2-omni",
            "mimo-v2-pro",
            "mimo-v2-tts",
        ):
            self.assertIn(old_model, proxy)
        self.assertIn("migrate_model_config(s_model", proxy)
        self.assertIn("AGENT_CFG_KEY_MODEL", proxy)
        self.assertIn("migrate_model_config(s_vision_model", proxy)
        self.assertIn("AGENT_CFG_KEY_VISION_MODEL", proxy)
        self.assertRegex(
            router,
            r"llm_normalize_model_for_host\(\s*b->host,\s*b->model\)",
        )
        self.assertIn("claw_config_set(key, migrated_json)", router)

    def test_custom_skill_has_required_markdown_workflow(self) -> None:
        skill = (AI_AGENT / "agent_skills/vela-desk.md").read_text()
        for section in ("# Vela Desktop Briefing", "## When to use", "## How to use", "## Example"):
            self.assertIn(section, skill)
        self.assertIn("/data/routine/status.json", skill)
        self.assertIn("not ai_agent cron jobs", skill)
        self.assertIn("/data/routine/config.json", skill)
        self.assertIn("live_mask", skill)
        self.assertIn("completed", skill)

    def test_music_capability_is_removed_without_affecting_voice_audio(self) -> None:
        registry = (AI_AGENT / "src/tools/tool_registry.c").read_text()
        loop = (AI_AGENT / "src/core/agent_loop.c").read_text()
        loader = (AI_AGENT / "src/tools/skill_loader.c").read_text()
        cmake = (AI_AGENT / "CMakeLists.txt").read_text()
        makefile = (AI_AGENT / "Makefile").read_text()

        for removed in (
            AI_AGENT / "agent_skills/music-dj.md",
            AI_AGENT / "agent_skills/sleep-music.md",
            AI_AGENT / "agent_skills/tts-speak.md",
            AI_AGENT / "src/tools/tool_media.c",
            AI_AGENT / "include/tools/tool_media.h",
        ):
            self.assertFalse(removed.exists(), f"retired music file remains: {removed}")

        for tool_name in (
            "music_search",
            "music_play",
            "music_pause",
            "music_resume",
            "music_stop",
            "music_seek",
            "music_set_volume",
            "music_status",
        ):
            self.assertNotIn(f'"{tool_name}"', registry)
            self.assertNotIn(f'"{tool_name}"', loop)

        self.assertNotIn("tool_media.c", cmake + makefile)
        self.assertNotIn('{ "music-dj",', loader)
        self.assertIn('remove_retired_skill("music-dj")', loader)
        self.assertIn('remove_retired_skill("sleep-music")', loader)
        self.assertIn('remove_retired_skill("tts-speak")', loader)
        self.assertIn("src/voice/audio_playback.c", cmake)
        self.assertIn("src/voice/voice_tts.c", cmake)

    def test_routine_schedule_update_tool_is_persistent_and_bounded(self) -> None:
        header = (AI_AGENT / "src/tools/tool_routine.h").read_text()
        source = (AI_AGENT / "src/tools/tool_routine.c").read_text()
        registry = (AI_AGENT / "src/tools/tool_registry.c").read_text()
        guard = (AI_AGENT / "src/tools/tool_guard.c").read_text()
        self.assertIn("tool_routine_schedule_update_execute", header)
        self.assertIn("ROUTINE_SCHEDULE_MAX 3", source)
        self.assertIn("expected_text", source)
        self.assertIn("ROUTINE_CONFIG_TMP_PATH", source)
        self.assertIn("rename(ROUTINE_CONFIG_TMP_PATH, ROUTINE_CONFIG_PATH)", source)
        self.assertIn('"routine_schedule_update"', registry)
        self.assertIn('"routine_schedule_update", TOOL_SEC_SENSITIVE', guard)

    def test_runtime_patch_contains_new_product_skill_and_tool(self) -> None:
        patch = (ROOT / "patches/packages_ai_agent/vela-desk-runtime.patch").read_text()
        self.assertIn("agent_skills/vela-desk.md", patch)
        self.assertIn("src/tools/tool_routine.c", patch)
        self.assertIn("routine_schedule_update", patch)

    def test_skill_is_registered_and_installed_to_device_directory(self) -> None:
        loader = (AI_AGENT / "src/tools/skill_loader.c").read_text()
        self.assertIn("BUILTIN_VELA_DESK", loader)
        self.assertIn('{ "vela-desk",      BUILTIN_VELA_DESK,', loader)
        self.assertIn("BUILTIN_PRODUCT_SKILL_VERSION", loader)
        self.assertIn("read_builtin_version", loader)
        self.assertIn("existing_version >= skill->version", loader)
        self.assertIn("BUILTIN_SKILL_MARKER(\"vela-desk\")", loader)
        self.assertIn("vela-builtin-skill:vela-desk:v3", (AI_AGENT / "agent_skills/vela-desk.md").read_text())
        self.assertIn('remove_retired_skill("music-dj")', loader)
        self.assertIn('AGENT_SKILLS_DIR, skill->filename', loader)
        self.assertIn('append_skill_summary("vela-desk.md"', loader)
        self.assertIn('strcmp(name, "vela-desk.md")', loader)
        self.assertIn('buf[size - 1] = \'\\0\';', loader)

    def test_shell_read_scope_contains_routine_without_write_scope(self) -> None:
        shell = (AI_AGENT / "src/tools/tool_shell.c").read_text()
        self.assertIn('"/data/routine/"', shell)
        self.assertIn('strncmp(command, "cat ", 4)', shell)

    def test_agent_core_starts_before_network_watcher(self) -> None:
        source = (AI_AGENT / "src/agent_main.c").read_text()
        core = source.index("int rc = agent_loop_start();")
        watcher = source.index('agent_task_create(network_watch_task')
        self.assertLess(core, watcher)
        self.assertIn('rc = ws_server_start();', source[core:watcher])

    def test_llm_latency_uses_monotonic_clock(self) -> None:
        loop = (AI_AGENT / "src/core/agent_loop.c").read_text()
        trace = (AI_AGENT / "src/core/agent_trace.c").read_text()
        self.assertIn("CLOCK_MONOTONIC", loop)
        self.assertNotIn("gettimeofday(&tv_start", loop)
        self.assertIn("start_mono_ms", trace)
        self.assertIn("CLOCK_MONOTONIC", trace)

    def test_cron_poll_debug_logging_is_opt_in(self) -> None:
        config = (AI_AGENT / "include/agent_config.h").read_text()
        kconfig = (AI_AGENT / "Kconfig").read_text()
        cron = (AI_AGENT / "src/infra/cron_service.c").read_text()
        self.assertIn("CONFIG_AI_AGENT_CRON_VERBOSE_CHECK", config)
        self.assertIn("#define AGENT_CRON_VERBOSE_CHECK 0", config)
        self.assertIn("config AI_AGENT_CRON_VERBOSE_CHECK", kconfig)
        self.assertIn("default n", kconfig[kconfig.index("config AI_AGENT_CRON_VERBOSE_CHECK"):])
        self.assertIn("#if AGENT_CRON_VERBOSE_CHECK", cron)

    def test_dynamic_requests_cannot_use_response_cache(self) -> None:
        cache_h = (AI_AGENT / "include/llm/llm_cache.h").read_text()
        cache_c = (AI_AGENT / "src/llm/llm_cache.c").read_text()
        loop = (AI_AGENT / "src/core/agent_loop.c").read_text()
        self.assertIn("llm_cache_prompt_is_safe", cache_h)
        self.assertIn('"你好"', cache_c)
        safe_start = cache_c.index("safe_prompts[]")
        safe_block = cache_c[safe_start:cache_c.index("};", safe_start)]
        for contextual_reply in ('"ok"', '"yes"', '"好的"', '"是"'):
            self.assertNotIn(contextual_reply, safe_block)
        self.assertIn("llm_cache_prompt_is_safe(msg->content", loop)
        self.assertIn("bool tool_was_used = false", loop)
        self.assertIn("cache_allowed && !tool_was_used", loop)

    def test_cron_remove_errors_are_authoritative(self) -> None:
        context = (AI_AGENT / "src/core/context_builder.c").read_text()
        registry = (AI_AGENT / "src/tools/tool_registry.c").read_text()
        cron_tool = (AI_AGENT / "src/tools/tool_cron.c").read_text()
        loop = (AI_AGENT / "src/core/agent_loop.c").read_text()
        self.assertIn("call cron_list and copy the exact current job ID", context)
        self.assertIn("never guess an ID", registry)
        self.assertIn("no job was removed", cron_tool)
        self.assertIn('strcmp(resp.calls[0].name, "cron_remove")', loop)
        self.assertIn('strncmp(tool_output, "ERROR:", 6)', loop)

    def test_context_distinguishes_routine_templates_from_cron(self) -> None:
        context = (AI_AGENT / "src/core/context_builder.c").read_text()
        self.assertIn("routine_mgr", context)
        self.assertIn("NOT cron jobs", context)
        self.assertIn("do not call cron_list", context)

    def test_routine_edit_fast_path_uses_status_then_persistent_tool(self) -> None:
        loop = (AI_AGENT / "src/core/agent_loop.c").read_text()
        registry = (AI_AGENT / "src/tools/tool_registry.c").read_text()
        self.assertIn("handle_routine_edit_fast_path", loop)
        self.assertIn("cat /data/routine/status.json", loop)
        self.assertIn('tool_registry_execute("routine_schedule_update"', loop)
        self.assertIn("read that status with run_shell (not read_file)", registry)


if __name__ == "__main__":
    unittest.main()
