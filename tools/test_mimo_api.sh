#!/bin/bash
# MIMO API 接入测试脚本
# 用于验证 API 端点、格式和响应

set -e

# 配置
API_KEY="${1:-your-api-key-here}"
if [ "$API_KEY" = "your-api-key-here" ]; then
    echo "用法: $0 <MIMO_API_KEY>"
    echo ""
    echo "请提供你的 MIMO API Key"
    echo "从 https://mimo.mi.com 获取"
    exit 1
fi

# 判断 API 端点
if [[ "$API_KEY" == tp-* ]]; then
    BASE_URL="https://token-plan-cn.xiaomimimo.com/v1"
    echo "✓ 使用 Token Plan 端点"
else
    BASE_URL="https://api.xiaomimimo.com/v1"
    echo "✓ 使用标准端点"
fi

echo "BASE_URL: $BASE_URL"
echo ""

# 临时文件
TMP_DIR="/tmp/mimo_test"
mkdir -p "$TMP_DIR"

# ============================================================
# 测试 1: Chat Completions (文本对话)
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 1: Chat Completions API"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# 测试请求
curl -s -X POST "$BASE_URL/chat/completions" \
  -H "api-key: $API_KEY" \
  -H "Content-Type: application/json" \
  -d '{
    "model": "mimo-v2.6",
    "messages": [
      {"role": "system", "content": "你是一个测试助手"},
      {"role": "user", "content": "请说你好"}
    ]
  }' > "$TMP_DIR/chat_result.json"

echo "原始响应:"
cat "$TMP_DIR/chat_result.json" | head -20
echo ""

# 解析响应
if grep -q '"content"' "$TMP_DIR/chat_result.json"; then
    echo "✓ Chat API 响应正常"

    # 检查响应结构
    if grep -q '"choices"' "$TMP_DIR/chat_result.json"; then
        echo "  响应格式: OpenAI 兼容 (choices 结构)"
    else
        echo "  响应格式: 简化格式 (直接 content)"
    fi
else
    echo "✗ Chat API 响应异常"
    echo "  可能的问题:"
    echo "    - API Key 无效"
    echo "    - 模型名称错误"
    echo "    - 请求格式错误"
fi
echo ""

# ============================================================
# 测试 2: ASR (语音转文字)
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 2: ASR API (音频格式支持)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# 生成测试音频 (1秒静音 PCM)
dd if=/dev/zero of="$TMP_DIR/test.pcm" bs=32000 count=1 2>/dev/null

echo "测试 PCM 格式上传..."
curl -s -X POST "$BASE_URL/audio/transcriptions" \
  -H "api-key: $API_KEY" \
  -F "file=@$TMP_DIR/test.pcm" \
  -F "model=mimo-v2.6" \
  > "$TMP_DIR/asr_pcm_result.json"

echo "PCM 响应:"
cat "$TMP_DIR/asr_pcm_result.json"
echo ""

# 检查是否需要 WAV 格式
if grep -qi "format\|invalid" "$TMP_DIR/asr_pcm_result.json"; then
    echo "⚠️  Raw PCM 可能不支持，尝试 WAV 格式..."

    # 生成 WAV 头 (简单的 44 字节 WAV 头 + PCM 数据)
    # 这里只是示例，真实环境需要正确的 WAV 封装
    echo "  需要转换为 WAV 格式"
else
    if grep -q '"text"' "$TMP_DIR/asr_pcm_result.json"; then
        echo "✓ ASR API 支持 PCM 格式"
    else
        echo "✗ ASR API 响应异常"
    fi
fi
echo ""

# ============================================================
# 测试 3: TTS (文字转语音)
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试 3: TTS API"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

curl -s -X POST "$BASE_URL/audio/speech" \
  -H "api-key: $API_KEY" \
  -H "Content-Type: application/json" \
  -d '{
    "model": "mimo-v2.6-tts",
    "input": "测试",
    "voice": "冰糖",
    "response_format": "pcm"
  }' \
  --output "$TMP_DIR/tts_output.pcm"

# 检查输出
if [ -f "$TMP_DIR/tts_output.pcm" ]; then
    SIZE=$(stat -c%s "$TMP_DIR/tts_output.pcm" 2>/dev/null || stat -f%z "$TMP_DIR/tts_output.pcm" 2>/dev/null)

    if [ "$SIZE" -gt 1000 ]; then
        echo "✓ TTS API 响应正常"
        echo "  输出文件: $TMP_DIR/tts_output.pcm"
        echo "  文件大小: $SIZE bytes"

        # 检查是否是二进制数据
        if file "$TMP_DIR/tts_output.pcm" | grep -qi "data"; then
            echo "  格式: 二进制音频数据"
        fi
    else
        echo "⚠️  TTS 输出文件太小，可能是错误响应"
        echo "  内容:"
        head -5 "$TMP_DIR/tts_output.pcm"
    fi
else
    echo "✗ TTS API 调用失败"
fi
echo ""

# ============================================================
# 总结与建议
# ============================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "测试总结"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "代码中需要确认的问题:"
echo ""
echo "1. 音频格式"
if grep -q '"text"' "$TMP_DIR/asr_pcm_result.json"; then
    echo "   ✓ ASR 支持 raw PCM 格式"
else
    echo "   ✗ ASR 可能需要 WAV 格式"
    echo "     建议: 在 voice_wakeup_service.c 中保存为 WAV"
fi
echo ""

echo "2. 响应解析"
if grep -q '"choices"' "$TMP_DIR/chat_result.json"; then
    echo "   ⚠️  Chat API 使用 OpenAI 兼容格式"
    echo "     当前代码: 直接查找 \"content\""
    echo "     正确格式: choices[0].message.content"
    echo "     需要修改: mimo_integration.c 中的 LLM 解析逻辑"
else
    echo "   ✓ Chat API 使用简化格式，当前解析逻辑正确"
fi
echo ""

echo "3. 模型版本"
echo "   当前使用: mimo-v2.6"
if grep -qi "model.*not.*found\|invalid.*model" "$TMP_DIR"/*.json; then
    echo "   ✗ 模型版本可能不正确"
    echo "     建议检查: https://mimo.mi.com/models"
else
    echo "   ✓ 模型版本正常"
fi
echo ""

echo "测试文件保存在: $TMP_DIR"
echo "建议查看原始响应以确认格式"
echo ""

# 清理选项
read -p "是否删除测试文件? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    rm -rf "$TMP_DIR"
    echo "已清理测试文件"
fi
