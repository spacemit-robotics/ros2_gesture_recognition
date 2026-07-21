"""Launch gesture_recognition node with config."""
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch_ros.substitutions import FindPackageShare
from launch_ros.actions import Node
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution


def generate_launch_description():
    params_file = PathJoinSubstitution(
        [FindPackageShare("gesture_recognition"), "config", "gesture_recognition.yaml"]
    )
    return LaunchDescription(
        [
            DeclareLaunchArgument("use_camera", default_value="true"),
            DeclareLaunchArgument("camera_id", default_value="1"),
            DeclareLaunchArgument("score_threshold", default_value="0.25"),
            Node(
                package="gesture_recognition",
                executable="gesture_recognition_node",
                name="gesture_recognition_node",
                output="screen",
                parameters=[
                    params_file,
                    {
                        "use_camera": LaunchConfiguration("use_camera"),
                        "camera_id": LaunchConfiguration("camera_id"),
                        "score_threshold": LaunchConfiguration("score_threshold"),
                    },
                ],
            )
        ]
    )
