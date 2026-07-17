/**
 * @file gesture_recognition_node.cpp
 * @brief Gesture recognition node: subscribes to image, runs gesture model,
 *        publishes boxes, optional Detection2DArray, debug image.
 *
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"
#ifdef HAVE_VISION_MSGS
#include "vision_msgs/msg/detection2_d_array.hpp"
#endif

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "gesture_recognition/detection_utils.h"
#include "gesture_recognition/image_utils.h"
#include "vision_service.h"

namespace {
// Default gesture labels (overridable via ROS parameter 'labels').
const std::vector<std::string> kDefaultGestureLabels = {
    "fist", "palm", "index", "ok", "peace", "thumbs_up", "thumbs_down",
    "rock", "paper", "scissors", "call", "like", "dislike", "stop", "one", "two", "three"
};
}  // namespace

class GestureRecognitionNode : public rclcpp::Node {
    public:
    GestureRecognitionNode() : Node("gesture_recognition_node") {
        std::string config_path = declare_parameter<std::string>("config_path", "");
        const bool lazy_load = declare_parameter<bool>("lazy_load", true);
        score_threshold_ = declare_parameter<double>("score_threshold", 0.25);
        if (!std::isfinite(score_threshold_) || score_threshold_ < 0.0 ||
            score_threshold_ > 1.0) {
            throw std::runtime_error("score_threshold out of range, must be in [0, 1]");
        }
        image_topic_ = declare_parameter<std::string>("image_topic", "/camera/image_raw");
        debug_image_topic_ =
            declare_parameter<std::string>("debug_image_topic", "/gesture_recognition/debug_image");
        boxes_topic_ = declare_parameter<std::string>("boxes_topic", "/gesture_recognition/boxes");
        use_camera_ = declare_parameter<bool>("use_camera", true);
        camera_id_ = declare_parameter<int>("camera_id", 0);
        camera_fps_ = declare_parameter<double>("camera_fps", 30.0);
        labels_ = declare_parameter<std::vector<std::string>>("labels", kDefaultGestureLabels);
#ifdef HAVE_VISION_MSGS
        gestures_topic_ = declare_parameter<std::string>("gestures_topic", "/perception/gestures");
#endif

        if (config_path.empty()) {
            config_path = GetDefaultConfigPath();
        }
        if (config_path.empty()) {
            throw std::runtime_error(
                "config_path is empty. Set config_path to vision model yaml "
                "(e.g. share/gesture_recognition/config/yolov5_gesture.yaml)");
        }

        service_ = VisionService::Create(config_path, "", lazy_load);
        if (!service_) {
            throw std::runtime_error("VisionService::Create failed: " + VisionService::LastCreateError());
        }

        boxes_pub_ = create_publisher<std_msgs::msg::Float32MultiArray>(boxes_topic_, 10);
#ifdef HAVE_VISION_MSGS
        gestures_pub_ = create_publisher<vision_msgs::msg::Detection2DArray>(gestures_topic_, 10);
#endif
        debug_pub_ =
            create_publisher<sensor_msgs::msg::Image>(debug_image_topic_, rclcpp::SensorDataQoS());

        if (use_camera_) {
            image_pub_ =
                create_publisher<sensor_msgs::msg::Image>(image_topic_, rclcpp::SensorDataQoS());
            cap_.open(camera_id_);
            if (!cap_.isOpened()) {
                throw std::runtime_error("gesture_recognition: cannot open camera id=" +
                                            std::to_string(camera_id_));
            }
            const int period_ms =
                (camera_fps_ > 1.0) ? static_cast<int>(1000.0 / camera_fps_) : 33;
            camera_timer_ = create_wall_timer(
                std::chrono::milliseconds(period_ms),
                std::bind(&GestureRecognitionNode::OnCameraTimer, this));
            RCLCPP_INFO(get_logger(),
                        "gesture_recognition_node: use_camera=true, publishing to %s, camera_id=%d",
                        image_topic_.c_str(), camera_id_);
        } else {
            image_sub_ = create_subscription<sensor_msgs::msg::Image>(
                image_topic_, rclcpp::SensorDataQoS(),
                std::bind(&GestureRecognitionNode::OnImage, this, std::placeholders::_1));
            RCLCPP_INFO(get_logger(),
                        "gesture_recognition_node: use_camera=false, subscribing to %s",
                        image_topic_.c_str());
        }

        RCLCPP_INFO(get_logger(),
                    "gesture_recognition_node started, config=%s, image_topic=%s",
                    config_path.c_str(), image_topic_.c_str());
    }

    private:
    static std::string GetDefaultConfigPath() {
        try {
            return ament_index_cpp::get_package_share_directory("gesture_recognition") +
                    "/config/yolov5_gesture.yaml";
        } catch (...) {
            return "";
        }
    }

    static const char* GetLabelName(int label_id, const std::vector<std::string>& labels) {
        if (label_id >= 0 && static_cast<size_t>(label_id) < labels.size())
            return labels[static_cast<size_t>(label_id)].c_str();
        static thread_local char buf[32];
        snprintf(buf, sizeof(buf), "class_%d", label_id);
        return buf;
    }

    void OnCameraTimer() {
        std_msgs::msg::Header header;
        header.stamp = now();
        header.frame_id = "camera";
        sensor_msgs::msg::Image img_msg;
        if (!gesture_recognition::CaptureCameraFrame(cap_, img_msg, header)) {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 3000, "camera read empty frame");
            return;
        }
        image_pub_->publish(img_msg);
        cv::Mat bgr = gesture_recognition::ImageMsgToBgr(img_msg);
        if (bgr.empty()) return;
        ProcessFrame(img_msg.header, bgr);
    }

    void OnImage(const sensor_msgs::msg::Image::SharedPtr msg) {
        cv::Mat bgr = gesture_recognition::ImageMsgToBgr(*msg);
        if (bgr.empty()) {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 3000,
                                    "invalid or unsupported image (encoding=%s)",
                                    msg->encoding.c_str());
            return;
        }
        ProcessFrame(msg->header, bgr);
    }

    void ProcessFrame(const std_msgs::msg::Header& header, const cv::Mat& bgr) {
        VisionServiceResponse response;
        if (service_->Infer(bgr, &response) != VISION_SERVICE_OK || !response.ok) {
            RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "infer failed: %s",
                                    service_->LastError().c_str());
            return;
        }

        auto boxes = ToDetectionBoxes(response.results);
        boxes = gesture_recognition::FilterByScore(boxes, static_cast<float>(score_threshold_));

        boxes_pub_->publish(gesture_recognition::EncodeBoxes(boxes, "num_gestures"));
#ifdef HAVE_VISION_MSGS
        gestures_pub_->publish(gesture_recognition::EncodeDetection2DArray(boxes, header));
#endif
        PublishDebugImage(header, bgr, boxes);
    }

    std::vector<gesture_recognition::DetectionBox> ToDetectionBoxes(
        const vision::ResultList& results) const {
        std::vector<gesture_recognition::DetectionBox> boxes;
        boxes.reserve(results.size());
        for (const auto& r : results) {
            const auto* det = std::get_if<vision::Detection>(&r);
            if (det == nullptr) continue;
            gesture_recognition::DetectionBox b;
            b.x1 = det->bbox.x1;
            b.y1 = det->bbox.y1;
            b.x2 = det->bbox.x2;
            b.y2 = det->bbox.y2;
            b.score = det->score;
            b.label = det->label;
            b.track_id = -1;
            b.class_name = GetLabelName(det->label, labels_);
            boxes.push_back(b);
        }
        return boxes;
    }

    void PublishDebugImage(const std_msgs::msg::Header& header, const cv::Mat& bgr,
                            const std::vector<gesture_recognition::DetectionBox>& boxes) {
        cv::Mat out_image = bgr.clone();
        const int font = cv::FONT_HERSHEY_SIMPLEX;
        const double font_scale = 0.9;
        const int thickness = 2;
        const cv::Scalar box_color(0, 255, 255);   // yellow box (BGR)
        const cv::Scalar text_bg(0, 0, 0);         // black background
        const cv::Scalar text_color(0, 255, 255);  // yellow text (BGR)
        for (const auto& b : boxes) {
            const int x1 = static_cast<int>(b.x1);
            const int y1 = static_cast<int>(b.y1);
            const int x2 = static_cast<int>(b.x2);
            const int y2 = static_cast<int>(b.y2);
            cv::rectangle(out_image, cv::Point(x1, y1), cv::Point(x2, y2), box_color, 2);
            std::string label_text =
                b.class_name + " " + std::to_string(static_cast<int>(b.score * 100)) + "%";
            int baseline = 0;
            cv::Size text_sz = cv::getTextSize(label_text, font, font_scale, thickness, &baseline);
            cv::rectangle(
                out_image,
                cv::Point(x1, y1 - text_sz.height - 6),
                cv::Point(x1 + text_sz.width + 2, y1),
                text_bg,
                -1);
            cv::putText(out_image, label_text, cv::Point(x1 + 1, y1 - 4), font, font_scale,
                        text_color, thickness);
        }
        debug_pub_->publish(gesture_recognition::BgrToImageMsg(out_image, header, "bgr8"));
    }

    std::unique_ptr<VisionService> service_;
    std::vector<std::string> labels_;
    double score_threshold_{0.25};
    std::string image_topic_, debug_image_topic_, boxes_topic_;
    bool use_camera_{true};
    int camera_id_{0};
    double camera_fps_{30.0};
    cv::VideoCapture cap_;
    rclcpp::TimerBase::SharedPtr camera_timer_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;
#ifdef HAVE_VISION_MSGS
    std::string gestures_topic_;
    rclcpp::Publisher<vision_msgs::msg::Detection2DArray>::SharedPtr gestures_pub_;
#endif
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
    rclcpp::Publisher<std_msgs::msg::Float32MultiArray>::SharedPtr boxes_pub_;
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr debug_pub_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    try {
        rclcpp::spin(std::make_shared<GestureRecognitionNode>());
    } catch (const std::exception& e) {
        RCLCPP_ERROR(rclcpp::get_logger("gesture_recognition_node"), "Exception: %s", e.what());
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
