"""Launch gesture_recognition node with config."""
from launch import LaunchDescription
from launch_ros.substitutions import FindPackageShare
from launch_ros.actions import Node
from launch.substitutions import PathJoinSubstitution


def generate_launch_description():
    params_file = PathJoinSubstitution(
        [FindPackageShare("gesture_recognition"), "config", "gesture_recognition.yaml"]
    )
    return LaunchDescription(
        [
            Node(
                package="gesture_recognition",
                executable="gesture_recognition_node",
                name="gesture_recognition_node",
                output="screen",
                parameters=[params_file],
            )
        ]
    )
