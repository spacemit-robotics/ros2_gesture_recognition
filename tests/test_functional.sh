#!/bin/bash
# Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
# SPDX-License-Identifier: Apache-2.0
set -eo pipefail
export PERCEPTION_MODULE=gesture_recognition
export PERCEPTION_NODE=gesture_recognition_node
export PERCEPTION_PACKAGES=gesture_recognition
export PERCEPTION_TOPIC=/gesture_recognition/boxes
export PERCEPTION_CONFIG_REL=config/yolov5_gesture.yaml
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "${SCRIPT_DIR}/vision_functional.sh"
