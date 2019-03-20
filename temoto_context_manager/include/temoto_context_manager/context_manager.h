#ifndef TEMOTO_CONTEXT_MANAGER__CONTEXT_MANAGER_H
#define TEMOTO_CONTEXT_MANAGER__CONTEXT_MANAGER_H

#include "temoto_core/common/base_subsystem.h"
#include "temoto_core/common/temoto_id.h"
#include "temoto_core/common/reliability.h"
#include "temoto_core/rmp/resource_manager.h"
#include "temoto_core/rmp/config_synchronizer.h"

#include "temoto_context_manager/context_manager_containers.h"
#include "temoto_context_manager/context_manager_services.h"
#include "temoto_context_manager/env_model_repository.h"

#include "temoto_nlp/task_manager.h"
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

  void loadGetNumberCb(GetNumber::Request& req, GetNumber::Response& res);

  void unloadGetNumberCb(GetNumber::Request& req, GetNumber::Response& res);

  /**
   * @brief loadTrackObjectCb
   * @param req
   * @param res
   */
  void loadTrackObjectCb(TrackObject::Request& req, TrackObject::Response& res);

  /**
   * @brief unloadTrackObjectCb
   * @param req
   * @param res
   */
  void unloadTrackObjectCb(TrackObject::Request& req, TrackObject::Response& res);

  void EMRSyncCb(const temoto_core::ConfigSync& msg, const Nodes& payload);
  
  bool updateEMRCb(UpdateEMR::Request& req, UpdateEMR::Response& res);

  bool getEMRNodeCb(GetEMRNode::Request& req, GetEMRNode::Response& res);

  void trackedObjectsSyncCb(const temoto_core::ConfigSync& msg, const std::string& payload);

  template<class Container>
  Container getContainer(NodePtr nodeptr)
  {
    return std::dynamic_pointer_cast<emr::ROSPayload<Container>>
      (nodeptr->getPayload())
        ->getPayload();
  }

  /**
   * @brief Get node as nodecontainer from EMR
   * 
   * @param name 
   * @param container 
   * @return true 
   * @return false 
   */
  bool getEMRNode(const std::string& name, std::string type, NodeContainer& container);
  /**
   * @brief Update the EMR structure with new information
   * 
   * @param nodes_to_add 
   * @param from_other_manager 
   * @return Nodes that could not be added
   */
  Nodes updateEMR(const Nodes & nodes_to_add, bool from_other_manager);

  /**
   * @brief Debug function to traverse through EMR tree 
   * 
   * @param root 
   */
  void traverseEMR(const emr::Node& root);
  
  /**
   * @brief Add or update a single node of the EMR
   * 
   * @tparam Container 
   * @param container 
   * @param container_type 
   */
  template <class Container>
  bool addOrUpdateEMRNode(const Container & container, const std::string& container_type);

  /**
   * @brief Advertise the EMR state through the config syncer
   * 
   */
  void advertiseEMR();

  /**
   * @brief Save the EMR state as a NodeContainer vector
   * 
   * @param emr 
   * @return Nodes 
   */
  Nodes EMRtoVector(const emr::EnvironmentModelRepository& emr);

  /**
   * @brief Recursive helper function to save EMR state
   * 
   * @param currentNode 
   * @param nodes 
   */
  void EMRtoVectorHelper(const emr::Node& currentNode, Nodes& nodes);

  ObjectPtr findObject(std::string object_name);

  NodePtr findNode(std::string name);

  void statusCb1(temoto_core::ResourceStatus& srv);

  void statusCb2(temoto_core::ResourceStatus& srv);

  void addDetectionMethod(std::string detection_method);

  void addDetectionMethods(std::vector<std::string> detection_methods);

  std::vector<std::string> getOrderedDetectionMethods();

  std::vector<std::string> getNodeDetectionMethods(const std::string& name);



  // Resource manager for handling servers and clients
  temoto_core::rmp::ResourceManager<ContextManager> resource_manager_1_;

  /*
   * Resource manager for handling servers and clients.
   * TODO: The second manager is used for making RMP calls within the same manager. If the same
   * resouce manager is used for calling servers managed by the same manager, the calls will lock
   */
  temoto_core::rmp::ResourceManager<ContextManager> resource_manager_2_;

  ros::NodeHandle nh_;

  ros::ServiceServer update_emr_server_;

  ros::ServiceServer get_emr_node_server_;

  ObjectPtrs objects_;

  std::map<int, std::string> m_tracked_objects_local_;

  std::map<std::string, std::string> m_tracked_objects_remote_;

  emr::EnvironmentModelRepository env_model_repository_;

  // Configuration syncer that manages external resource descriptions and synchronizes them
  // between all other (context) managers
  temoto_core::rmp::ConfigSynchronizer<ContextManager, Nodes> EMR_syncer_;

  temoto_core::rmp::ConfigSynchronizer<ContextManager, std::string> tracked_objects_syncer_;

  temoto_nlp::TaskManager tracker_core_;

  std::map<std::string, temoto_core::Reliability> detection_method_history_;

  std::pair<int, std::string> active_detection_method_;
};
} // temoto_context_manager namespace

#endif
