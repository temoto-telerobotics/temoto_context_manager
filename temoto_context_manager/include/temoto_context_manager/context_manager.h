/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright 2019 TeMoto Telerobotics
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/* Author: Robert Valner */
/* Author: Meelis Pihlap */

#ifndef TEMOTO_CONTEXT_MANAGER__CONTEXT_MANAGER_H
#define TEMOTO_CONTEXT_MANAGER__CONTEXT_MANAGER_H

#include "temoto_core/common/base_subsystem.h"
#include "temoto_core/common/temoto_id.h"
#include "temoto_core/common/reliability.h"
#include "temoto_core/trr/resource_registrar.h"
#include "temoto_core/trr/config_synchronizer.h"

#include "temoto_context_manager/context_manager_services.h"
#include "temoto_context_manager/context_manager_containers.h"
#include "temoto_context_manager/env_model_interface.h"
#include "temoto_context_manager/emr_ros_interface.h"
#include "temoto_context_manager/emr_item_to_component_link.h"

#include "temoto_action_engine/action_engine.h"
#include "temoto_component_manager/component_manager_services.h"

namespace temoto_context_manager
{

class ContextManager : public temoto_core::BaseSubsystem
{
public:
  ContextManager();

  const std::string& getName() const
  {
    return subsystem_name_;
  }

private:

  void loadTrackObjectCb(TrackObject::Request& req, TrackObject::Response& res);

  void unloadTrackObjectCb(TrackObject::Request& req, TrackObject::Response& res);

  void emrSyncCb(const temoto_core::ConfigSync& msg, const Items& payload);
  
  bool updateEmrCb(UpdateEmr::Request& req, UpdateEmr::Response& res);

  bool getEmrItemCb(GetEMRItem::Request& req, GetEMRItem::Response& res);

  bool getEmrVectorCb(GetEMRVector::Request& req, GetEMRVector::Response& res);

  void trackedObjectsSyncCb(const temoto_core::ConfigSync& msg, const std::string& payload);

  /**
   * @brief Get node as nodecontainer from EMR
   * 
   * @param name 
   * @param container 
   * @return true 
   * @return false 
   */
  template <class Container> 
  bool getEmrItemHelper(const std::string& name, std::string type, ItemContainer& container);
  /**
   * @brief Get an item from EMR 
   * 
   * @param name 
   * @param type 
   * @param container 
   * @return true 
   * @return false 
   */
  bool getEmrItem(const std::string& name, std::string type, ItemContainer& container);
  /**
   * @brief Update the EMR structure with new information
   * 
   * @param items_to_add 
   * @param from_other_manager 
   * @return Items that could not be added
   */
  Items updateEmr(const Items & items_to_add, bool from_other_manager, bool update_time=false);
  
  /**
   * @brief Advertise the EMR state through the config syncer
   * 
   */
  void advertiseEmr();

  ObjectPtr findObject(std::string object_name);

  void statusCb1(temoto_core::ResourceStatus& srv);

  void statusCb2(temoto_core::ResourceStatus& srv);

  void addDetectionMethod(std::string detection_method);

  void addDetectionMethods(std::vector<std::string> detection_methods);

  std::vector<std::string> getOrderedDetectionMethods();

  std::vector<std::string> getItemDetectionMethods(const std::string& name);

  template <class Container>
  std::string parseContainerType();

  /**
   * @brief Invokes an action that tries to find connections between component manager components
   * and EMR items
   */
  void startComponentToEmrLinker();

  /**
   * @brief Tries to retrieve the required parameters of pipe segments 
   * 
   * @param pipe_info_msg 
   * @param load_pipe_msg 
   * @param pipe_category 
   * @param requested_emr_item_name 
   * @return true no parameters were required or all parameters were specified
   * @return false if at least one parameter was left unspecified
   */
  bool getParameterSpecifications( const temoto_component_manager::Pipe& pipe_info_msg
                                 , temoto_component_manager::LoadPipe& load_pipe_msg
                                 , const std::string& pipe_category
                                 , const std::string& requested_emr_item_name);

  void timerCallback(const ros::TimerEvent&);

  // Resource manager for handling servers and clients
  temoto_core::trr::ResourceRegistrar<ContextManager> resource_registrar_1_;

  /*
   * Resource manager for handling servers and clients.
   * TODO: The second manager is used for making RMP calls within the same manager. If the same
   * resouce manager is used for calling servers managed by the same manager, the calls will lock
   */
  temoto_core::trr::ResourceRegistrar<ContextManager> resource_registrar_2_;

  ros::NodeHandle nh_;

  ros::ServiceServer update_emr_server_;

  ros::ServiceServer get_emr_item_server_;

  ros::ServiceServer get_emr_vector_server_;

  ObjectPtrs objects_;

  std::map<int, std::string> m_tracked_objects_local_;

  std::map<std::string, std::string> m_tracked_objects_remote_;

  emr::EnvironmentModelRepository env_model_repository_;

  std::shared_ptr<EnvModelInterface> emr_interface; 

  ros::Timer emr_sync_timer;


  // Configuration syncer that manages external resource descriptions and synchronizes them
  // between all other (context) managers
  temoto_core::trr::ConfigSynchronizer<ContextManager, Items> emr_syncer_;

  temoto_core::trr::ConfigSynchronizer<ContextManager, std::string> tracked_objects_syncer_;

  ActionEngine action_engine_;

  ComponentToEmrRegistry component_to_emr_registry_;

  std::map<std::string, temoto_core::Reliability> detection_method_history_;

  std::pair<int, std::string> active_detection_method_;

  std::map<std::string, std::string> parameter_map_ =
  {
    {"frame_id", emr_ros_interface::emr_containers::COMPONENT},
    {"odom_frame_id", emr_ros_interface::emr_containers::ROBOT},
    {"base_frame_id", emr_ros_interface::emr_containers::ROBOT}
  };
};

} // temoto_context_manager namespace

#endif
