#include "ros/package.h"
#include "temoto_context_manager/context_manager.h"
#include <algorithm>
#include <utility>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include "temoto_core/common/ros_serialization.h"

namespace temoto_context_manager
{

ContextManager::ContextManager()
  : temoto_core::BaseSubsystem("temoto_context_manager", temoto_core::error::Subsystem::CONTEXT_MANAGER, __func__)
  , resource_manager_1_(srv_name::MANAGER, this)
  , resource_manager_2_(srv_name::MANAGER_2, this)
  , tracked_objects_syncer_(srv_name::MANAGER, srv_name::SYNC_TRACKED_OBJECTS_TOPIC, &ContextManager::trackedObjectsSyncCb, this)
  , EMR_syncer_(srv_name::MANAGER, srv_name::SYNC_OBJECTS_TOPIC, &ContextManager::EMRSyncCb, this)
  , tracker_action_engine_(this, false, ros::package::getPath(ROS_PACKAGE_NAME) + "/config/action_dst.yaml")
{
  /*
   * Start the servers
   */

  // TODO: This is just an example service
  resource_manager_1_.addServer<GetNumber>( srv_name::GET_NUMBER_SERVER
                                                    , &ContextManager::loadGetNumberCb
                                                    , &ContextManager::unloadGetNumberCb);

  // Object tracking service
  resource_manager_1_.addServer<TrackObject>(srv_name::TRACK_OBJECT_SERVER
                                                    , &ContextManager::loadTrackObjectCb
                                                    , &ContextManager::unloadTrackObjectCb);

  // Register callback for status info
  resource_manager_1_.registerStatusCb(&ContextManager::statusCb1);
  resource_manager_1_.registerStatusCb(&ContextManager::statusCb2);

  // "Update EMR" server
  update_emr_server_ = nh_.advertiseService(srv_name::SERVER_UPDATE_EMR, &ContextManager::updateEMRCb, this);
  
  // Request remote EMR configurations
  EMR_syncer_.requestRemoteConfigs();
  
  TEMOTO_INFO("Context Manager is ready.");
}

/*
 * Implementation of the GetNumber service
 */ 
void ContextManager::loadGetNumberCb( GetNumber::Request& req
                                    , GetNumber::Response& res)
{
  TEMOTO_INFO_STREAM("Received a request to load number '" << req.requested_int << "'");
  res.responded_int = req.requested_int;
}

/*
 * Implementation of the unload GetNumber service
 */ 
void ContextManager::unloadGetNumberCb( GetNumber::Request& req
                                      , GetNumber::Response& res)
{
  (void)res; // Suppress "unused parameter" compiler warnings
  TEMOTO_INFO_STREAM("Received a request to UNload number '" << req.requested_int << "'");
}

/*
 * EMR synchronization callback
 */
void ContextManager::EMRSyncCb(const temoto_core::ConfigSync& msg, const Items& payload)
{
  if (msg.action == temoto_core::rmp::sync_action::REQUEST_CONFIG)
  {
    advertiseEMR();
    return;
  }

  // Add or update objects
  if (msg.action == temoto_core::rmp::sync_action::ADVERTISE_CONFIG)
  {
    TEMOTO_DEBUG("Received a payload.");
    updateEMR(payload, true);
  }
}

/*
 * Tracked objects synchronization callback
 */
void ContextManager::trackedObjectsSyncCb(const temoto_core::ConfigSync& msg, const std::string& payload)
{
  if (msg.action == temoto_core::rmp::sync_action::ADVERTISE_CONFIG)
  {
    TEMOTO_DEBUG_STREAM("Received a message, that '" << payload << "' is tracked by '"
                        << msg.temoto_namespace << "'.");

    // Add a notion about a object that is being tracked
    m_tracked_objects_remote_[payload] = msg.temoto_namespace;
  }
  else
  if (msg.action == temoto_core::rmp::sync_action::REMOVE_CONFIG)
  {
    TEMOTO_DEBUG_STREAM("Received a message, that '" << payload << "' is not tracked by '"
                        << msg.temoto_namespace << "' anymore.");

    // Remove a notion about a object that is being tracked
    m_tracked_objects_remote_.erase(payload);
  }
}

Items ContextManager::updateEMR(const Items& items_to_add, bool from_other_manager, bool update_time)
{
  
  // Keep track of failed add/update attempts
  std::vector<ItemContainer> failed_items = emr_interface.updateEMR(items_to_add, update_time);

  // If this object was added by its own namespace, then advertise this config to other managers
  if (!from_other_manager)
  {
    TEMOTO_INFO("Advertising EMR to other namespaces.");
    advertiseEMR(); 
  }
  return failed_items;
}

/*
 * Advertise all objects
 */
// 
void ContextManager::advertiseEMR()
{
  // Publish all items 
  Items items_payload = emr_interface.EMRtoVector();
  // If there is something to send, advertise.
  if (items_payload.size()) 
  {
    EMR_syncer_.advertise(items_payload);
  }
}
std::vector<std::string> ContextManager::getItemDetectionMethods(const std::string& name)
{
  ItemPtr itemptr = env_model_repository_.getItemByName(name);
  if (itemptr == nullptr) 
  {
    throw CREATE_ERROR(temoto_core::error::Code::UNKNOWN_OBJECT, "Item " + name + " not found!");
  }
  TEMOTO_INFO_STREAM("The requested item is known");
  std::string type = itemptr->getPayload()->getType();
  if (type == "OBJECT") 
  {
    ObjectContainer obj = getContainer<ObjectContainer>(itemptr);
    return obj.detection_methods;
  }
  else if (type == "MAP") 
  {
    MapContainer map = getContainer<MapContainer>(itemptr);
    return map.detection_methods;
  }
  else if (type == "COMPONENT")
  {
    throw CREATE_ERROR(temoto_core::error::Code::INVALID_CONTAINER_TYPE, "Item of type COMPONENT has no detection methods!");
  }
  else
  {
    throw CREATE_ERROR(temoto_core::error::Code::INVALID_CONTAINER_TYPE, "Item type not recognized!");
  }
}
/*
 * Find object
 */
ObjectPtr ContextManager::findObject(std::string object_name)
{
  for (auto& object : objects_)
  {
    if (object->name == object_name)
    {
      return object;
    }
  }

  // Throw an error if no objects were found
  throw CREATE_ERROR(temoto_core::error::Code::UNKNOWN_OBJECT, "The requested object is unknown");
}


/*
 * Callback for adding objects
 */
bool ContextManager::updateEMRCb(UpdateEMR::Request& req, UpdateEMR::Response& res)
{
  (void)res; // Suppress "unused variable" compiler warnings
  TEMOTO_INFO("Received a request to add %ld item(s) to the EMR.", req.items.size());

  res.failed_items = ContextManager::updateEMR(req.items, false);
  return true;
}

/*
 * Server for tracking objects
 */
void ContextManager::loadTrackObjectCb(TrackObject::Request& req, TrackObject::Response& res)
{
  try
  {
    /*
     * Replace all spaces in the name with the underscore character because the now deprecated
     * ObjectContainer API converted all object names into this format. 
     */ 
    std::string item_name_no_space = req.object_name;
    std::replace(item_name_no_space.begin(), item_name_no_space.end(), ' ', '_');

    TEMOTO_INFO_STREAM("Received a request to track an object named: '" << item_name_no_space << "'");

    /*
     * Check if this object is already tracked in other instance of TeMoto. If thats the case, then
     * relay the request to the remote TeMoto instance. The response from the remote instance is 
     * forwarded back to the initial client that initiated the query.
     * 
     * M_TODO_lp: Implement a functionality that allows to see what and where 
     * (in terms of local/remote instances) objects are actively tracked., i.e., uncomment and
     * modify the following block.
     */

    // std::string remote_temoto_namespace = m_tracked_objects_remote_[item_name_no_space];

    // if (!remote_temoto_namespace.empty())
    // {
    //   TEMOTO_DEBUG_STREAM("The object '" << item_name_no_space << "' is alerady tracked by '"
    //                       << remote_temoto_namespace << "'. Forwarding the request.");

    //   TrackObject track_object_msg;
    //   track_object_msg.request = req;

    //   // Send the request to the remote namespace
    //   resource_manager_2_.template call<TrackObject>(srv_name::MANAGER,
    //                                                  srv_name::TRACK_OBJECT_SERVER,
    //                                                  track_object_msg,
    //                                                  temoto_core::rmp::FailureBehavior::NONE,
    //                                                  remote_temoto_namespace);

    //   res = track_object_msg.response;
    //   return;
    // }

    /*
     * Look if the requested object is described in the object database
     * 
     * M_TODO_hp: Find and return the item. It is up to you how you would
     * like to return this information but the main thing is that we need
     * to access the "detection_methods" field of a serialized container
     */ 
    std::vector<std::string> detection_methods = getItemDetectionMethods(item_name_no_space);

    /*
     * Start a pipe that provides the raw data for tracking the requested object
     */
    temoto_component_manager::LoadPipe load_pipe_msg;
    // addDetectionMethods(detection_methods);


    std::string selected_pipe;

    /*
     * Loop over different pipe categories and try to load one. The loop is iterated either until
     * a pipe is succesfully loaded or options are exhausted (failure)
     */
    for (auto& pipe_category : detection_methods)
    {
      try
      {
        TEMOTO_INFO_STREAM("Trying to track the " << item_name_no_space
                           << " via '"<< pipe_category << "'");

        load_pipe_msg = temoto_component_manager::LoadPipe();
        load_pipe_msg.request.pipe_category = pipe_category;
        resource_manager_1_.call<temoto_component_manager::LoadPipe>(temoto_component_manager::srv_name::MANAGER_2,
                                                                     temoto_component_manager::srv_name::PIPE_SERVER,
                                                                     load_pipe_msg);

        selected_pipe = pipe_category;
        // detection_method_history_[pipe_category].adjustReliability();
        active_detection_method_.first = load_pipe_msg.response.rmp.resource_id;
        active_detection_method_.second = pipe_category;
        break;
      }
      catch (temoto_core::error::ErrorStack& error_stack)
      {
        // detection_method_history_[pipe_category].adjustReliability(0);

        // If the requested pipe was not found but there are other options
        // available, then continue. Otherwise forward the error
        if (error_stack.front().code == static_cast<int>(temoto_core::error::Code::NO_TRACKERS_FOUND) &&
            &pipe_category != &detection_methods.back())
        {
          continue;
        }
        else
        {
          throw FORWARD_ERROR(error_stack);
        }
      }
    }

    /*
     * Start the object tracker. Since there are different general object
     * tracking methods and each tracker outputs different types of data, then
     * the specific tracking has to be set up based on the general tracker. For example
     * a general tracker, e.g. AR tag detector, publshes data about detected tags. The
     * specific tracker has to subscribe to the detected tags topic and since the
     * tags are differentiated by the tag ID, the specific tracker has to know the ID
     * beforehand.
     */

    /*
     * Get the topic where the tracker publishes its output data
     */
    temoto_core::TopicContainer pipe_topics;
    pipe_topics.setOutputTopicsByKeyValue(load_pipe_msg.response.output_topics);

    // Topic where the information about the required object is going to be published.
    std::string tracked_object_topic = temoto_core::common::getAbsolutePath("object_tracker/" + item_name_no_space);

    /*
     * Object tracker setup
     */
    TEMOTO_DEBUG_STREAM("Using " << selected_pipe << " based tracking");

    /*
     * Action related stuff up ahead: A semantic frame is manually created. Based on that SF
     * a SF tree is created, given that an action implementation, that corresponds to the
     * manually created SF, exists. The tracker action is invoked  and it continues
     * running in the background until its ordered to stop.
     */

    std::string action = "track";
    temoto_nlp::Subjects subjects;

    // Subject that will contain the name of the tracked object.
    // Necessary when the tracker has to be stopped
    temoto_nlp::Subject sub_0("what", item_name_no_space);

    // Subject that will contain the data necessary for the specific tracker
    temoto_nlp::Subject sub_1("what", selected_pipe);

    // Topic where the action must publish the data about the tracked object
    sub_1.addData("topic", tracked_object_topic);

    // Pass the topic container so that the action can access the pipe
    sub_1.addData("pointer", boost::any_cast<temoto_core::TopicContainer>(pipe_topics));

    // Pass a pointer to EMR interface, which will be used to access EMR without ROS messaging overhead
    sub_1.addData("pointer", boost::any_cast<EMR_ROS_Interface::EMR_ROS_interface*>(&emr_interface));

    subjects.push_back(sub_0);
    subjects.push_back(sub_1);

    // Create a SF
    std::vector<temoto_nlp::TaskDescriptor> task_descriptors;
    task_descriptors.emplace_back(action, subjects);
    task_descriptors[0].setActionStemmed(action);

    // Create a sematic frame tree
    temoto_nlp::TaskTree sft = temoto_nlp::SFTBuilder::build(task_descriptors);

    // Get the root node of the tree
    temoto_nlp::TaskTreeNode& root_node = sft.getRootNode();
    sft.printTaskDescriptors(root_node);

    // Execute the SFT
    tracker_action_engine_.executeSFT(std::move(sft));

    // Put the object into the list of tracked objects. This is used later
    // for stopping the tracker
    m_tracked_objects_local_[res.rmp.resource_id] = item_name_no_space;

    res.object_topic = tracked_object_topic;

    // Let context managers in other namespaces know, that this object is being tracked
    tracked_objects_syncer_.advertise(item_name_no_space);

  }
  catch (temoto_core::error::ErrorStack& error_stack)
  {
    throw FORWARD_ERROR(error_stack);
  }
}

/*
 * Unload the track object
 */
void ContextManager::unloadTrackObjectCb(TrackObject::Request& req,
                                         TrackObject::Response& res)
{
  /*
   * Stopping tracking the object based on its name
   */
  try
  {
    // Check if the object is tracked locally or by a remote manager
    if (!m_tracked_objects_remote_[req.object_name].empty())
    {
      // The object is tracked by a remote manager
      return;
    }

    // Get the name of the tracked object
    std::string tracked_object = m_tracked_objects_local_[res.rmp.resource_id];

    if (tracked_object.empty())
    {
      throw CREATE_ERROR(temoto_core::error::Code::NO_TRACKERS_FOUND, std::string("The object '") +
                         req.object_name + "' is not tracked");
    }

    TEMOTO_DEBUG_STREAM("Received a request to stop tracking an object named: '"
                        << tracked_object << "'");

    // Stop tracking the object
    tracker_action_engine_.stopTask("", tracked_object);

    // Erase the object from the map of tracked objects
    m_tracked_objects_local_.erase(res.rmp.resource_id);

    // Let context managers in other namespaces know, that this object is not tracked anymore
    tracked_objects_syncer_.advertise(tracked_object, temoto_core::rmp::sync_action::REMOVE_CONFIG);
  }
  catch (temoto_core::error::ErrorStack& error_stack)
  {
    throw FORWARD_ERROR(error_stack);
  }
}


/*
 * Status callback 1
 */
void ContextManager::statusCb1(temoto_core::ResourceStatus& srv)
{
  (void)srv; // Suppress "unused parameter" compiler warnings
  /* TODO */
}

/*
 * Status callback 2
 */
void ContextManager::statusCb2(temoto_core::ResourceStatus& srv)
{
  (void)srv; // Suppress the "unused parameter" warning
  TEMOTO_DEBUG("Received a status message.");
}

void ContextManager::addDetectionMethod(std::string detection_method)
{
  if (detection_method_history_.find(detection_method) == detection_method_history_.end())
  {
    detection_method_history_[detection_method] = temoto_core::Reliability();
    std::cout << "Added new detection method !!!!! \n";
  }
}

void ContextManager::addDetectionMethods(std::vector<std::string> detection_methods)
{
  for (std::string& detection_method : detection_methods)
  {
    addDetectionMethod(detection_method);
  }
}

std::vector<std::string> ContextManager::getOrderedDetectionMethods()
{
  std::vector<std::pair<std::string, temoto_core::Reliability>> ordered_detection_methods;

  for ( auto it = detection_method_history_.begin()
      ; it != detection_method_history_.end()
      ; it++)
  {
    ordered_detection_methods.push_back(*it);
  }

  std::sort( ordered_detection_methods.begin()
           , ordered_detection_methods.end()
           , [](const std::pair<std::string, temoto_core::Reliability>& lhs, const std::pair<std::string, temoto_core::Reliability>& rhs)
             {
               return lhs.second.getReliability() >
                      rhs.second.getReliability();
             });

  std::vector<std::string> odm_vec;

  for (auto odm : ordered_detection_methods)
  {
    std::cout << odm.first << " --- " << odm.second.getReliability() << std::endl;
    odm_vec.push_back(odm.first);
  }

  return odm_vec;
}

}  // namespace temoto_context_manager
