#ifndef TEMOTO_EMR_ROS_INTERFACE_H
#define TEMOTO_EMR_ROS_INTERFACE_H

#include <algorithm>
#include <tf/transform_broadcaster.h>
#include <ros/ros.h>

#include "temoto_context_manager/context_manager_containers.h"
#include "temoto_context_manager/env_model_repository.h"

#include "temoto_core/common/ros_serialization.h"
#include "ros/package.h"

namespace emr_ros_interface
{

template <class RosMsg>
class RosPayload : public emr::PayloadEntry
{
private:
  RosMsg payload_;
  std::string maintainer_;
public:
  void updateTime()
  {
    payload_.pose.header.stamp = ros::Time::now();
  }
  void updateTime(ros::Time new_time)
  {
    payload_.pose.header.stamp = new_time;
  }
  ros::Time getTime() {return payload_.pose.header.stamp;}

  std::string getMaintainer() {return maintainer_;}

  void setMaintainer(const std::string& maintainer)
  {
    maintainer_ = maintainer;
  }
  /**
   * @brief Get the Name object
   * 
   * @return std::string 
   */
  const std::string& getName() const
  {
    return payload_.name;
  }
  RosMsg getPayload() const {return payload_;};
  /**
   * @brief Set the Payload object
   * 
   * @param payload 
   */
  void setPayload(RosMsg & payload) {payload_ = payload;};

  RosPayload(RosMsg payload) : payload_(payload)
  {
  }
  RosPayload(RosMsg payload, std::string maintainer) 
    : payload_(payload), maintainer_(maintainer)
  {
  }

};

class EmrRosInterface
{
public:
  EmrRosInterface(emr::EnvironmentModelRepository& emr, std::string identifier) : env_model_repository_(emr), identifier_(identifier) 
  {
    tf_timer_ = nh_.createTimer(ros::Duration(0.1), &EmrRosInterface::emr_tf_callback, this);
  }

  template<class Container>
  Container getContainer(const std::string name)
  {
    return getRosPayloadPtr<Container>(name)->getPayload();
  }
  template<class Container>
  std::shared_ptr<Container> getContainerPtr(const std::string name)
  {
    return std::make_shared<Container>(getRosPayloadPtr<Container>(name)->getPayload());
  }
  template<class Container>
  std::shared_ptr<RosPayload<Container>> getRosPayloadPtr(const std::string& name)
  {
    return std::dynamic_pointer_cast<RosPayload<Container>>
      (env_model_repository_.getItemByName(name)->getPayload());
  }
  bool hasItem(const std::string& name) {return env_model_repository_.hasItem(name);}

  /**
   * @brief Update the EMR structure with new information
   * 
   * @param items_to_add 
   * @param from_other_manager 
   * @return std::vector<temoto_context_manager::ItemContainer> that could not be added
   */
  std::vector<temoto_context_manager::ItemContainer> updateEmr(const std::vector<temoto_context_manager::ItemContainer> & items_to_add, bool update_time=false);

  /**
   * @brief Debug function to traverse through EMR tree 
   * 
   * @param root 
   */
  void traverseEmr(const emr::Item& root);
  
  /**
   * @brief Add or update a single item of the EMR
   * 
   * @tparam Container 
   * @param container 
   * @param container_type 
   */
  template <class Container>
  bool addOrUpdateEmrItem(const Container & container, 
                          const std::string& container_type, 
                          const temoto_context_manager::ItemContainer& ic, 
                          const bool update_time);

  /**
   * @brief Save the EMR state as a temoto_context_manager::ItemContainer vector
   * 
   * @param emr 
   * @return std::vector<temoto_context_manager::ItemContainer> 
   */
  std::vector<temoto_context_manager::ItemContainer> EmrToVector();

  /**
   * @brief Recursive helper function to save EMR state
   * 
   * @param currentItem 
   * @param items 
   */
  void EmrToVectorHelper(const emr::Item& currentItem, std::vector<temoto_context_manager::ItemContainer>& items);
private:
  emr::EnvironmentModelRepository& env_model_repository_;
  std::string identifier_;
  ros::NodeHandle nh_;
  ros::Timer tf_timer_;
  tf::TransformBroadcaster tf_broadcaster;
  
  void emr_tf_callback(const ros::TimerEvent&);
  template <class Container>
  void publish_container_tf(const std::string& type, const Container& container);
};

} // namespace emr_ros_interface

#endif