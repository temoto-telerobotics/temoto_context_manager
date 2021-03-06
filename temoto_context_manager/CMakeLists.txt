cmake_minimum_required(VERSION 2.8.3)
project(temoto_context_manager)

## Compile as C++11, supported in ROS Kinetic and newer
add_compile_options(-std=c++1y)

## Find catkin macros and libraries
## if COMPONENTS list like find_package(catkin REQUIRED COMPONENTS xyz)
## is used, also find other catkin packages
find_package(catkin REQUIRED COMPONENTS
  roscpp
  roslib
  message_generation
  geometry_msgs
  shape_msgs
  visualization_msgs
  diagnostic_msgs
  tf
  tf2
  tf2_ros
  tf2_geometry_msgs
  temoto_core
  temoto_action_engine
  temoto_component_manager
)

add_message_files(FILES 
  ObjectContainer.msg
  MapContainer.msg
  ItemContainer.msg
  ComponentContainer.msg
  RobotContainer.msg
)

add_service_files(
  FILES
  TrackObject.srv
  UpdateEmr.srv
  GetEMRItem.srv
  GetEMRVector.srv
)

generate_messages(
  DEPENDENCIES
  std_msgs
  geometry_msgs
  shape_msgs
  visualization_msgs
  diagnostic_msgs
  temoto_core
  temoto_action_engine
)

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES yaml-cpp
  CATKIN_DEPENDS roscpp roslib temoto_component_manager temoto_core temoto_er_manager temoto_action_engine
#  DEPENDS system_lib
)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)

add_executable(temoto_context_manager 
  src/context_manager_node.cpp
  src/context_manager.cpp
  src/context_manager_containers.cpp
  src/env_model_repository.cpp
  src/emr_ros_interface.cpp
  src/emr_item_to_component_link.cpp
)

add_dependencies(temoto_context_manager
  ${catkin_EXPORTED_TARGETS}
  ${${PROJECT_NAME}_EXPORTED_TARGETS}
)
target_link_libraries(temoto_context_manager
  ${catkin_LIBRARIES}
)
