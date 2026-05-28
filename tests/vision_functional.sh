#!/bin/bash
# Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
# SPDX-License-Identifier: Apache-2.0
# Functional PR test for vision-style perception nodes.
# Required env: PERCEPTION_MODULE, PERCEPTION_NODE, PERCEPTION_PACKAGES,
#               PERCEPTION_TOPIC, PERCEPTION_CONFIG_REL

set -eo pipefail

: "${PERCEPTION_MODULE:?}"
: "${PERCEPTION_NODE:?}"
: "${PERCEPTION_PACKAGES:?}"
: "${PERCEPTION_TOPIC:?}"
: "${PERCEPTION_CONFIG_REL:?}"
REQUIRE_TOPIC_DATA="${PERCEPTION_REQUIRE_TOPIC_DATA:-0}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/common_test_utils.sh"

echo "=== ${PERCEPTION_MODULE} Functional Test ==="

NODE_PID=""
PUB_PID=""
ECHO_PID=""
cleanup_test_processes() {
  if [ -n "${ECHO_PID}" ]; then
    kill "${ECHO_PID}" 2>/dev/null || true
  fi
  ECHO_PID=""
  stop_node_gracefully "${PUB_PID}"
  PUB_PID=""
  stop_node_gracefully "${NODE_PID}"
  NODE_PID=""
  [ -n "${MISSING_MODEL_CONFIG:-}" ] && rm -f "${MISSING_MODEL_CONFIG}" 2>/dev/null || true
}
trap cleanup_test_processes EXIT INT TERM

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/common_setup.sh"

TEST_INVALID="${SCRIPT_DIR}/test_config_invalid.yaml"
PKG_CONFIG="$(pkg_share_config "${PERCEPTION_MODULE}" "${PERCEPTION_CONFIG_REL}")"
ABS_VALID="${SCRIPT_DIR}/test_config_valid.yaml"
MISSING_MODEL_CONFIG="$(mktemp "${TMPDIR:-/tmp}/perception_missing_model_${PERCEPTION_MODULE}.XXXXXX.yaml")"
MISSING_MODEL_PATH="$(mktemp -u "${TMPDIR:-/tmp}/perception_missing_model_${PERCEPTION_MODULE}.XXXXXX.onnx")"
rm -f "${MISSING_MODEL_PATH}" 2>/dev/null || true
awk -v missing_path="${MISSING_MODEL_PATH}" '
  BEGIN { replaced=0 }
  /^model_path:/ && !replaced { print "model_path: " missing_path; replaced=1; next }
  { print }
' "${ABS_VALID}" >"${MISSING_MODEL_CONFIG}"

if [ ! -f "${PKG_CONFIG}" ]; then
  echo "FAIL: package config not found at ${PKG_CONFIG} (install broken?)" >&2
  exit 1
fi

echo "Test 1: missing model in config (expect node error)"
assert_node_fails_with_error \
  "ros2 run ${PERCEPTION_MODULE} ${PERCEPTION_NODE} --ros-args \
    -p config_path:=${MISSING_MODEL_CONFIG} \
    -p use_camera:=false \
    -p lazy_load:=false" \
  "VisionService::Create failed|Failed to load|runtime_error|Exception"

echo "Test 2: invalid config (expect node error)"
assert_node_fails_with_error \
  "ros2 run ${PERCEPTION_MODULE} ${PERCEPTION_NODE} --ros-args \
    -p config_path:=${TEST_INVALID} \
    -p use_camera:=false \
    -p lazy_load:=false" \
  "YAML|parse|Failed|VisionService::Create failed|runtime_error|Exception"

echo "Test 3: lazy_load node starts and advertises topics"
ros2 run "${PERCEPTION_MODULE}" "${PERCEPTION_NODE}" --ros-args \
  -p "config_path:=${PKG_CONFIG}" \
  -p lazy_load:=true \
  -p use_camera:=false &
NODE_PID=$!
sleep 3

assert_node_starts "${PERCEPTION_NODE}"
assert_topic_exists "${PERCEPTION_TOPIC}"

stop_node_gracefully "${NODE_PID}"
NODE_PID=""

# Wait for topic to disappear after node stops
sleep 2

MODEL_PATH="$(grep '^model_path:' "${PKG_CONFIG}" | head -1 | sed 's/^model_path:[[:space:]]*//')"
MODEL_PATH="${MODEL_PATH//\"/}"
MODEL_PATH="${MODEL_PATH//\'/}"
MODEL_PATH="${MODEL_PATH/#\~/$HOME}"
if [ -f "${MODEL_PATH}" ] && command -v python3 >/dev/null 2>&1; then
  echo "Test 4: topic data with mock image (model present)"
  ros2 run "${PERCEPTION_MODULE}" "${PERCEPTION_NODE}" --ros-args \
    -p "config_path:=${PKG_CONFIG}" \
    -p use_camera:=false \
    -p lazy_load:=false \
    -p image_topic:=/test/image_raw &
  NODE_PID=$!

  # Wait for node to be ready (topic advertised and model loaded)
  echo "Waiting for ${PERCEPTION_TOPIC} to be advertised..."
  TOPIC_READY=0
  for i in {1..30}; do
    if ros2 topic list 2>/dev/null | grep -Fxq "${PERCEPTION_TOPIC}"; then
      # Topic exists, but wait a bit more to ensure model is loaded
      sleep 3
      echo "Topic ready after ${i}s"
      TOPIC_READY=1
      break
    fi
    sleep 1
  done

  if [ "${TOPIC_READY}" -ne 1 ]; then
    echo "FAIL: ${PERCEPTION_TOPIC} not advertised within 30s" >&2
    exit 1
  fi

    ECHO_FILE="${TMPDIR:-/tmp}/perception_topic_echo_${PERCEPTION_MODULE}_$$.txt"
    rm -f "${ECHO_FILE}"
    timeout 30s ros2 topic echo "${PERCEPTION_TOPIC}" --once >"${ECHO_FILE}" 2>/dev/null &
    ECHO_PID=$!
    sleep 0.5
    python3 "${SCRIPT_DIR}/publish_test_image.py" --topic /test/image_raw --count 30 &
    PUB_PID=$!

    set +e
    wait "${ECHO_PID}"
    ECHO_RC=$?
    set -e
    ECHO_PID=""

    # Let publisher finish cleanly (avoid rcl context errors in log).
    wait "${PUB_PID}" 2>/dev/null || stop_node_gracefully "${PUB_PID}"
    PUB_PID=""
    stop_node_gracefully "${NODE_PID}"
    NODE_PID=""
    if [ "${ECHO_RC}" -eq 0 ] && [ -s "${ECHO_FILE}" ]; then
      echo "PASS: received output on ${PERCEPTION_TOPIC}"
    elif [ "${REQUIRE_TOPIC_DATA}" = "1" ]; then
      echo "FAIL: no output on ${PERCEPTION_TOPIC} (strict mode enabled)" >&2
      exit 1
    else
      echo "WARN: no output on ${PERCEPTION_TOPIC} (non-strict PR mode, continuing)" >&2
    fi
else
  echo "SKIP Test 4: model not found or python3 not available"
fi

echo "=== ${PERCEPTION_MODULE} functional tests passed ==="
