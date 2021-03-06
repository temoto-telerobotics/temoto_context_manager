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

#include "ros/package.h"
#include "temoto_core/common/ros_serialization.h"
#include "temoto_context_manager/context_manager.h"
#include <algorithm>
#include <utility>
#include <yaml-cpp/yaml.h>
#include <fstream>

namespace temoto_context_manager
{

ContextManager::ContextManager()
  : temoto_core::BaseSubsystem("temoto_context_manager", temoto_core::error::Subsystem::CONTEXT_MANAGER, __func__)
  , resource_registrar_1_(srv_name::MANAGER, this)
  , resource_registrar_2_(srv_name::MANAGER_2, this)
  , tracked_objects_syncer_(srv_name::MANAGER, srv_name::SYNC_TRACKED_OBJECTS_TOPIC, &ContextManager::trackedObjectsSyncCb, this)
  , emr_syncer_(srv_name::MANAGER, srv_name::SYNC_OBJECTS_TOPIC, &ContextManager::emrSyncCb, this)
  , action_engine_()
{
  /*
   * Set up the action engine
   */ 
  std::string action_uri_file_path = ros::package::getPath(ROS_PACKAGE_NAME) + "/config/action_dst.yaml";

  // Open the action sources yaml file and get the paths to action libs
  // TODO: check for typos and other existance problems
  YAML::Node config = YAML::LoadFile(action_uri_file_path);
  TEMOTO_INFO_STREAM("Indexing TeMoto actions");
  for (YAML::const_iterator it = config.begin(); it != config.end(); ++it)
  {
    std::string package_name = (*it)["package_name"].as<std::string>();
    std::string relative_path = (*it)["relative_path"].as<std::string>();
    std::string full_path = ros::package::getPath(package_name) + "/" + relative_path;
    action_engine_.addActionsPath(full_path);
  }

  // Start the Action Engine
  action_engine_.start();

  /*
   * Initialize the Environment Model Repository interface
   */
  emr_interface = std::make_shared<emr_ros_interface::EmrRosInterface>(env_model_repository_, temoto_core::common::getTemotoNamespace());
  
  /*
   * Start the servers
   */

  // Object tracking service
  resource_registrar_1_.addServer<TrackObject>(srv_name::TRACK_OBJECT_SERVER
                                            , &ContextManager::loadTrackObjectCb
                                            , &ContextManager::unloadTrackObjectCb);

  // Register callback for status info
  resource_registrar_1_.registerStatusCb(&ContextManager::statusCb1);
  resource_registrar_2_.registerStatusCb(&ContextManager::statusCb2);

  // "Update EMR" 
  TEMOTO_INFO("Starting the EMR update server");
  update_emr_server_ = nh_.advertiseService(srv_name::SERVER_UPDATE_EMR, &ContextManager::updateEmrCb, this);
  get_emr_item_server_ = nh_.advertiseService(srv_name::SERVER_GET_EMR_ITEM, &ContextManager::getEmrItemCb, this);

  get_emr_vector_server_ = nh_.advertiseService(srv_name::SERVER_GET_EMR_VECTOR, &ContextManager::getEmrVectorCb, this);
  
  // Request remote EMR configurations
  emr_syncer_.requestRemoteConfigs();

  emr_sync_timer = nh_.createTimer(ros::Duration(1), &ContextManager::timerCallback, this);

  // Start the component-to-EMR linker actions
  TEMOTO_INFO("Starting the component-to-emr-item linker ...");
  startComponentToEmrLinker();
  
  TEMOTO_INFO("Context Manager is ready.");
}

// TODO: Do we need this? 
void ContextManager::timerCallback(const ros::TimerEvent&)
{
  // Request remote EMR configurations
  TEMOTO_DEBUG_STREAM("Syncing EMR");
  emr_syncer_.requestRemoteConfigs();
}
/*
 * EMR synchronization callback
 */
void ContextManager::emrSyncCb(const temoto_core::ConfigSync& msg, const Items& payload)
{
  if (msg.action == temoto_core::trr::sync_action::REQUEST_CONFIG)
  {
    advertiseEmr();
    return;
  }

  // Add or update objects
  if (msg.action == temoto_core::trr::sync_action::ADVERTISE_CONFIG)
  {
    TEMOTO_DEBUG("Received a payload.");
    updateEmr(payload, true);
  }
}

/*
 * Tracked objects synchronization callback
 */
void ContextManager::trackedObjectsSyncCb(const temoto_core::ConfigSync& msg, const std::string& payload)
{
  if (msg.action == temoto_core::trr::sync_action::ADVERTISE_CONFIG)
  {
    TEMOTO_DEBUG_STREAM("Received a message, that '" << payload << "' is tracked by '"
                        << msg.temoto_namespace << "'.");

    // Add a notion about a object that is being tracked
    m_tracked_objects_remote_[payload] = msg.temoto_namespace;
  }
  else
  if (msg.action == temoto_core::trr::sync_action::REMOVE_CONFIG)
  {
    TEMOTO_DEBUG_STREAM("Received a message, that '" << payload << "' is not tracked by '"
                        << msg.temoto_namespace << "' anymore.");

    // Remove a notion about a object that is being tracked
    m_tracked_objects_remote_.erase(payload);
  }
}

Items ContextManager::updateEmr(const Items& items_to_add, bool from_other_manager, bool update_time)
{
  
  // Keep track of failed add/update attempts
  std::vector<ItemContainer> failed_items = emr_interface->updateEmr(items_to_add, update_time);

  // If this object was added by its own namespace, then advertise this config to other managers
  if (!from_other_manager)
  {
    TEMOTO_INFO("Advertising EMR to other namespaces.");
    advertiseEmr(); 
  }
  return failed_items;
}

/*
 * Advertise all objects
 */

void ContextManager::advertiseEmr()
{
  // Publish all items 
  Items items_payload = emr_interface->EmrToVector();
  // If there is something to send, advertise.
  if (items_payload.size()) 
  {
    emr_syncer_.advertise(items_payload);
  }
}
template <class Container>
std::string ContextManager::parseContainerType()
{
  if (std::is_same<Container, temoto_context_manager::ObjectContainer>::value) 
  {
    return emr_ros_interface::emr_containers::OBJECT;
  }
  else if (std::is_same<Container, temoto_context_manager::MapContainer>::value) 
  {
    return emr_ros_interface::emr_containers::MAP;
  }
  else if (std::is_same<Container, temoto_context_manager::ComponentContainer>::value) 
  {
    return emr_ros_interface::emr_containers::COMPONENT;
  }
  else if (std::is_same<Container, temoto_context_manager::RobotContainer>::value) 
  {
    return emr_ros_interface::emr_containers::ROBOT;
  }
  ROS_ERROR_STREAM("UNRECOGNIZED TYPE");
  return "FAULTY_TYPE";
}

bool ContextManager::getEmrItem(const std::string& name, std::string type, ItemContainer& container)
{
  std::string real_type = emr_interface->getTypeByName(name);
  // Check if requested type matches real type
  if (real_type == type) 
  {
    if (type == emr_ros_interface::emr_containers::OBJECT) 
    {
      auto rospl = emr_interface->getObject(name);
      container.serialized_container = temoto_core::serializeROSmsg(rospl);
      container.type = type;
      return true;
    }
    else if (type == emr_ros_interface::emr_containers::MAP) 
    {
      auto rospl = emr_interface->getMap(name);
      container.serialized_container = temoto_core::serializeROSmsg(rospl);
      container.type = type;
      return true;
    }
    else if (type == emr_ros_interface::emr_containers::COMPONENT) 
    {
      auto rospl = emr_interface->getComponent(name);
      container.serialized_container = temoto_core::serializeROSmsg(rospl);
      container.type = type;
      return true;
    }
    else if (type == emr_ros_interface::emr_containers::ROBOT) 
    {
      auto rospl = emr_interface->getRobot(name);
      container.serialized_container = temoto_core::serializeROSmsg(rospl);
      container.type = type;
      return true;
    }
    else
    {
      TEMOTO_ERROR_STREAM("Unrecognized container type specified: " << type << std::endl);
      return false;
    }
  }
  else
  {
    TEMOTO_ERROR_STREAM("Wrong type requested for EMR node with name: " << name << std::endl);
    TEMOTO_ERROR_STREAM("Requested type: " << type << std::endl);
    TEMOTO_ERROR_STREAM("Actual type: "<< real_type << std::endl);
    return false;
  }
}
bool ContextManager::getEmrVectorCb(GetEMRVector::Request& req, GetEMRVector::Response& res)
{
  res.items = emr_interface->EmrToVector();
  return true;
}
std::vector<std::string> ContextManager::getItemDetectionMethods(const std::string& name)
{
  if (!emr_interface->hasItem(name))
  {
    throw CREATE_ERROR(temoto_core::error::Code::UNKNOWN_OBJECT, "Item " + name + " not found!");
  }
  TEMOTO_INFO_STREAM("The requested item is known");
  std::string type = emr_interface->getTypeByName(name);
  if (type == emr_ros_interface::emr_containers::OBJECT) 
  {
    ObjectContainer obj = emr_interface->getObject(name);
    return obj.detection_methods;
  }
  else if (type == emr_ros_interface::emr_containers::MAP) 
  {
    MapContainer map = emr_interface->getMap(name);
    return map.detection_methods;
  }
  else if (type == emr_ros_interface::emr_containers::ROBOT) 
  {
    RobotContainer robot = emr_interface->getRobot(name);
    return robot.detection_methods;
  }
  else if (type == emr_ros_interface::emr_containers::COMPONENT)
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
bool ContextManager::updateEmrCb(UpdateEmr::Request& req, UpdateEmr::Response& res)
{
  (void)res; // Suppress "unused variable" compiler warnings
  TEMOTO_INFO("Received a request to add %ld item(s) to the EMR.", req.items.size());

  res.failed_items = ContextManager::updateEmr(req.items, false);
  return true;
}

bool ContextManager::getEmrItemCb(GetEMRItem::Request& req, GetEMRItem::Response& res)
{
  TEMOTO_INFO_STREAM("Received a request to get item: " << req.name << "from the EMR." << std::endl);
  ItemContainer nc;
  
  res.success = ContextManager::getEmrItem(req.name, req.type, nc);
  res.item = nc;
  TEMOTO_WARN_STREAM("t1 " << res.success);
  return true;
}

/*
 * Server for tracking objects
 */
void ContextManager::loadTrackObjectCb(TrackObject::Request& req, TrackObject::Response& res)
{
  try
  {
    TEMOTO_INFO_STREAM("Received a request to track an object named: '" << req.object_name << "'");

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
    //   resource_registrar_2_.template call<TrackObject>(srv_name::MANAGER,
    //                                                  srv_name::TRACK_OBJECT_SERVER,
    //                                                  track_object_msg,
    //                                                  temoto_core::trr::FailureBehavior::NONE,
    //                                                  remote_temoto_namespace);

    //   res = track_object_msg.response;
    //   return;
    // }

    /*
     * Look if the requested object is described in the object database
     */ 
    std::vector<std::string> detection_methods = getItemDetectionMethods(req.object_name);

    /*
     * Start a pipe that provides the raw data for tracking the requested object
     */
    temoto_component_manager::LoadPipe load_pipe_msg;
    // addDetectionMethods(detection_methods);
    std::string selected_pipe;
    load_pipe_msg.request.use_only_local_segments = req.use_only_local_resources;

    /*
     * Loop over different pipe categories and try to load one. The loop is iterated either until
     * a pipe is succesfully loaded or options are exhausted (failure)
     */
    for (auto& pipe_category : detection_methods)
    {
      // Check if this type of pipe exists in the registry
      if (!component_to_emr_registry_.hasPipe(pipe_category))
      {
        TEMOTO_ERROR_STREAM("Could not locate pipe: " << pipe_category);
        continue;
      }

      try
      {
        temoto_component_manager::Pipe pipe_info_msg;
        if (!component_to_emr_registry_.getPipeByType(pipe_category, pipe_info_msg))
        {
          continue;
        }

        TEMOTO_INFO_STREAM("Trying to track the " << req.object_name
                           << " via '"<< pipe_category << "'");
        load_pipe_msg = temoto_component_manager::LoadPipe(); // Clear the message

        /*
         * Check if any segments of this pipe require knowledge about any geometrical 
         * parameters ,i.e., frames
         */
        if (!getParameterSpecifications(pipe_info_msg, load_pipe_msg, pipe_category, req.object_name))
        {
          continue;
        }

        load_pipe_msg.request.pipe_category = pipe_category;
        resource_registrar_1_.call<temoto_component_manager::LoadPipe>(temoto_component_manager::srv_name::MANAGER_2,
                                                                     temoto_component_manager::srv_name::PIPE_SERVER,
                                                                     load_pipe_msg);

        selected_pipe = pipe_category;
        // detection_method_history_[pipe_category].adjustReliability();
        active_detection_method_.first = load_pipe_msg.response.trr.resource_id;
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
    std::string item_name_no_space = req.object_name;
    std::replace(item_name_no_space.begin(), item_name_no_space.end(), ' ', '_');
    std::string tracked_object_topic = temoto_core::common::getAbsolutePath("object_tracker/" + item_name_no_space);

    /*
     * Object tracker setup
     */
    TEMOTO_DEBUG_STREAM("Using " << selected_pipe << " based tracking");

    /*
     * Action related stuff up ahead: An UMRF is manually created, that corresponds an Action.
     * The tracker action is invoked  and it continues to run in the background until its ordered to stop.
     */

    Umrf track_object_umrf;
    track_object_umrf.setName("TaTrackCmObject");
    track_object_umrf.setSuffix("0");
    track_object_umrf.setEffect("synchronous");

    ActionParameters ap;
    ap.setParameter("tracked_object::name", "string", boost::any_cast<std::string>(req.object_name));
    ap.setParameter("tracked_object::output_topic", "string", boost::any_cast<std::string>(tracked_object_topic));
    ap.setParameter("pipe::name", "string", boost::any_cast<std::string>(selected_pipe));
    ap.setParameter("pipe::topic", "temoto_core::TopicContainer", boost::any_cast<temoto_core::TopicContainer>(pipe_topics));
    ap.setParameter("emr", "std::shared_ptr<EnvModelInterface>", boost::any_cast<std::shared_ptr<EnvModelInterface>>(emr_interface));

    track_object_umrf.setInputParameters(ap);
    std::string umrf_graph_name = item_name_no_space + "_graph";
    action_engine_.executeUmrfGraph(umrf_graph_name, std::vector<Umrf>{track_object_umrf}, true);

    /*
     * Put the umrf graph name into the list of tracked objects. This is used later
     * for stopping the tracker action
     */ 
    m_tracked_objects_local_[res.trr.resource_id] = umrf_graph_name;
    res.object_topic = tracked_object_topic;

    /*
     * Let context managers in other namespaces know, that this object is being tracked
     */ 
    tracked_objects_syncer_.advertise(item_name_no_space);
  }
  catch (temoto_core::error::ErrorStack& error_stack)
  {
    throw FORWARD_ERROR(error_stack);
  }
}

/*
 * startComponentToEMRLinker
 */ 
void ContextManager::startComponentToEmrLinker()
{
  /*
   * Invoke an Action that continuously links Context Manager components
   * with EMR objects
   */
  try
  {
    Umrf track_object_umrf;
    track_object_umrf.setName("TaEmrComponentLinker");
    track_object_umrf.setSuffix("0");
    track_object_umrf.setEffect("synchronous");

    ActionParameters ap;
    ap.setParameter("emr", "std::shared_ptr<EnvModelInterface>", boost::any_cast<std::shared_ptr<EnvModelInterface>>(emr_interface));
    ap.setParameter("emr-to-component registry", "ComponentToEmrRegistry*", boost::any_cast<ComponentToEmrRegistry*>(&component_to_emr_registry_));

    track_object_umrf.setInputParameters(ap);
    action_engine_.executeUmrfGraph("emr_component_linker_graph", std::vector<Umrf>{track_object_umrf}, true);
  }
  catch (temoto_core::error::ErrorStack& error_stack)
  {
    // TODO: Figure out what is the best way to propagate errors from the constructor
    TEMOTO_ERROR("Problems with starting the emr-component linker action");
    //throw FORWARD_ERROR(error_stack);
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
    std::string tracked_object = m_tracked_objects_local_[res.trr.resource_id];

    if (tracked_object.empty())
    {
      throw CREATE_ERROR(temoto_core::error::Code::NO_TRACKERS_FOUND, std::string("The object '") +
                         req.object_name + "' is not tracked");
    }

    TEMOTO_DEBUG_STREAM("Received a request to stop tracking an object named: '"
                        << tracked_object << "'");

    // Stop tracking the object
    action_engine_.stopUmrfGraph(tracked_object);

    // Erase the object from the map of tracked objects
    m_tracked_objects_local_.erase(res.trr.resource_id);

    // Let context managers in other namespaces know, that this object is not tracked anymore
    tracked_objects_syncer_.advertise(tracked_object, temoto_core::trr::sync_action::REMOVE_CONFIG);
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

void addSpecifierToSegment( diagnostic_msgs::KeyValue& parameter
                          , std::vector<temoto_component_manager::PipeSegmentSpecifier>& seg_specifiers
                          , unsigned int segment_index)
{
  bool seg_specifier_exists = false;
  for (auto& seg_specifier : seg_specifiers)
  {
    if (seg_specifier.segment_index == segment_index)
    {
      seg_specifier.parameters.push_back(parameter);
      seg_specifier_exists = true;
      break;
    }
  }
  if (!seg_specifier_exists)
  {
    temoto_component_manager::PipeSegmentSpecifier seg_specifier;
    seg_specifier.segment_index = segment_index;
    seg_specifier.parameters.push_back(parameter);
    seg_specifiers.push_back(seg_specifier);
  }
}

bool ContextManager::getParameterSpecifications( const temoto_component_manager::Pipe& pipe_info_msg
                                               , temoto_component_manager::LoadPipe& load_pipe_msg
                                               , const std::string& pipe_category
                                               , const std::string& requested_emr_item_name)
{
  /*
   * Check if any segments of this pipe require knowledge about any geometrical 
   * parameters ,i.e., frames
   */
  std::vector<diagnostic_msgs::KeyValue*> spec_ptrs;
  std::vector<diagnostic_msgs::KeyValue*> post_spec_ptrs;

  for (unsigned int i=0; i<pipe_info_msg.segments.size(); i++)
  {
    const temoto_component_manager::PipeSegment& pipe_segment = pipe_info_msg.segments[i];
    const std::vector<std::string>& required_params = pipe_segment.required_parameters;

    /*
     * Loop through the required parameters
     */ 
    for (const auto& required_param : required_params)
    {
      /*
       * Frame ID specification
       */ 
      if (required_param == "frame_id")
      {
        TEMOTO_DEBUG("Segment %d (type: %s) of pipe '%s' requires 'frame_id' parameter specifications"
                , i, pipe_segment.segment_type.c_str(), pipe_category.c_str());

        temoto_component_manager::PipeSegmentSpecifier pipe_seg_spec;
        diagnostic_msgs::KeyValue frame_id_spec;

        // Check if there are any emr-linked components that have the required type (e.g., 2D camera)
        ComponentInfos component_infos = component_to_emr_registry_.hasLinks(pipe_segment.segment_type);
        if (!component_infos.empty())
        {
          TEMOTO_DEBUG("Segment %d (type: %s) of pipe '%s' can be specified in-place"
                    , i, pipe_segment.segment_type.c_str(), pipe_category.c_str());

          // TODO: Implement a selection metric
          temoto_component_manager::Component& chosen_component = component_infos[0];
          frame_id_spec.key = "frame_id";
          frame_id_spec.value = chosen_component.component_name;
          pipe_seg_spec.component_name = chosen_component.component_name; 
          pipe_seg_spec.segment_index = i;
          load_pipe_msg.request.pipe_segment_specifiers.push_back(pipe_seg_spec);
          addSpecifierToSegment(frame_id_spec, load_pipe_msg.request.pipe_segment_specifiers, i);
          load_pipe_msg.request.pipe_name = pipe_info_msg.pipe_name;

          // TODO: That's the most horriffic beast i've ever created. Slay it asap. The idea is
          // that instead of maintaining indexes to pipe segments, its simpler to keep the pointers
          // to specific parameters
          spec_ptrs.push_back(&(load_pipe_msg.request.pipe_segment_specifiers.back().parameters.back()));
        }
        else
        {
          TEMOTO_DEBUG("Segment %d (type: %s) of pipe '%s' requires post-specification"
                    , i, pipe_segment.segment_type.c_str(), pipe_category.c_str());

          // If no emr-linked components were found then this is either an currently not defined
          // EMR item, or this component does not have geometry, i.e., it's an algorithm
          // Mark this component to be assessed after each segment has been checked
          frame_id_spec.key = "frame_id";
          addSpecifierToSegment(frame_id_spec, load_pipe_msg.request.pipe_segment_specifiers, i);

          // TODO: That's the most horriffic beast i've ever created. Slay it asap. The idea is
          // that instead of maintaining indexes to pipe segments, its simpler to keep the pointers
          // to specific parameters
          post_spec_ptrs.push_back(&(load_pipe_msg.request.pipe_segment_specifiers.back().parameters.back()));
        }
      }
      
      /*
       * Odometry Frame ID specification
       */
      else if (required_param == "odom_frame_id")
      {
        TEMOTO_DEBUG("Segment %d (type: %s) of pipe '%s' requires 'odom_frame_id' parameter specifications"
                    , i, pipe_segment.segment_type.c_str(), pipe_category.c_str());
        try
        {
          diagnostic_msgs::KeyValue odom_frame_id_spec;
          RobotContainer rc = emr_interface->getRobot(requested_emr_item_name);
          odom_frame_id_spec.key = "odom_frame_id";
          odom_frame_id_spec.value = temoto_core::common::getTemotoNamespace() + "/" + rc.odom_frame_id; 
          addSpecifierToSegment(odom_frame_id_spec, load_pipe_msg.request.pipe_segment_specifiers, i);
          load_pipe_msg.request.pipe_name = pipe_info_msg.pipe_name;
        }
        catch(const std::exception& e)
        {
          std::cerr << e.what() << '\n';
          return false;
        }
      }

      /*
       * Base Link Frame ID specification
       */
      else if (required_param == "base_frame_id")
      {
        TEMOTO_DEBUG("Segment %d (type: %s) of pipe '%s' requires 'base_frame_id' parameter specifications"
                    , i, pipe_segment.segment_type.c_str(), pipe_category.c_str());
        try
        {
          RobotContainer rc = emr_interface->getRobot(requested_emr_item_name);
          diagnostic_msgs::KeyValue base_frame_id_spec;
          base_frame_id_spec.key = "base_frame_id";
          base_frame_id_spec.value = temoto_core::common::getTemotoNamespace() + "/" + rc.base_frame_id; 
          addSpecifierToSegment(base_frame_id_spec, load_pipe_msg.request.pipe_segment_specifiers, i);
          load_pipe_msg.request.pipe_name = pipe_info_msg.pipe_name;
        }
        catch(const std::exception& e)
        {
          std::cerr << e.what() << '\n';
          return false;
        }
      }

      /*
       * Map topic specification
       */
      else if (required_param == "map_topic")
      {
        MapContainer mc = emr_interface->getNearestParentMap(requested_emr_item_name);
        diagnostic_msgs::KeyValue map_topic_spec;
        map_topic_spec.key = "map_topic";
        map_topic_spec.value = mc.topic;
        addSpecifierToSegment(map_topic_spec, load_pipe_msg.request.pipe_segment_specifiers, i);
        load_pipe_msg.request.pipe_name = pipe_info_msg.pipe_name;
      }

      /*
       * Map Frame ID specification
       */
      else if (required_param == "global_frame_id")
      {
        MapContainer mc = emr_interface->getNearestParentMap(requested_emr_item_name);
        diagnostic_msgs::KeyValue map_frame_id_spec;
        map_frame_id_spec.key = "global_frame_id";
        map_frame_id_spec.value = mc.name;
        addSpecifierToSegment(map_frame_id_spec, load_pipe_msg.request.pipe_segment_specifiers, i);
        load_pipe_msg.request.pipe_name = pipe_info_msg.pipe_name;
      }

      /*
       * TF prefix specification
       */
      else if (required_param == "tf_prefix")
      {
        diagnostic_msgs::KeyValue tf_prefix_spec;
        tf_prefix_spec.key = "tf_prefix";
        tf_prefix_spec.value = temoto_core::common::getTemotoNamespace();
        addSpecifierToSegment(tf_prefix_spec, load_pipe_msg.request.pipe_segment_specifiers, i);
        load_pipe_msg.request.pipe_name = pipe_info_msg.pipe_name;
      }
    }

    /*
     * Check if there were any post spec segments
     */ 
    if (!post_spec_ptrs.empty())
    {
      TEMOTO_DEBUG("Trying to post-specify %lu segments of pipe '%s'", post_spec_ptrs.size()
        , pipe_category.c_str());

      // If this pipe contains segments that need specifications but cannot be specified
      // then this pipe cannot be used
      if (spec_ptrs.empty())
      {
        TEMOTO_DEBUG("Cannot post-specify any segments of pipe '%s' because there are no"
          "in-place specificationss", pipe_category.c_str());
        return false;
      }
      
      // Go through the parameters which need post 
      for (auto post_spec_ptr : post_spec_ptrs)
      {
        // Look the spec information from specified parameters
        for (auto spec_ptr : spec_ptrs)
        {
          if (post_spec_ptr->key == spec_ptr->key)
          {
            TEMOTO_DEBUG("Post-specifying '%s'(key) as '%s'(value)", post_spec_ptr->key.c_str()
              , spec_ptr->value.c_str());
            // TODO: the post_spec_ptr->value might be overwritten
            post_spec_ptr->value = spec_ptr->value;
          }
        } 
      }

      // Check if all post parameters have been specified
      bool parameters_specified = true;
      for (auto post_spec_ptr : post_spec_ptrs)
      {
        if (post_spec_ptr->value.empty())
        {
          parameters_specified = false;
          break;
        }
      }

      // If some parameters are still without a value, then this pipe
      // cannot be used
      if (!parameters_specified)
      {
        return false;
      }
    }
  }
  return true;
}

}  // namespace temoto_context_manager
