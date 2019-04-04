#include "temoto_context_manager/emr_ros_interface.h"

namespace emr_ros_interface
{
template <class Container>
void EmrRosInterface::publish_container_tf(const std::string& type, const Container& container)
{
  tf::Transform transform;
  transform.setOrigin(tf::Vector3(container.pose.pose.position.x,
                                  container.pose.pose.position.y,
                                  container.pose.pose.position.z));
  transform.setRotation(tf::Quaternion(container.pose.pose.orientation.x,
                                      container.pose.pose.orientation.y,
                                      container.pose.pose.orientation.z,
                                      container.pose.pose.orientation.w));
  tf_broadcaster.sendTransform(tf::StampedTransform(transform, container.pose.header.stamp, container.parent, container.name));
}
void EmrRosInterface::emr_tf_callback(const ros::TimerEvent&)
{
  for (auto const& item_entry : env_model_repository_.getItems())
  {
    // If root node, tf can not be published
    if (item_entry.second->getParent().expired()) continue;

    std::string type = item_entry.second->getPayload()->getType();
    if (type == "OBJECT")
    {
      // If maintainer is another instance, don't publish
      if (getRosPayloadPtr<temoto_context_manager::ObjectContainer>(item_entry.first)->getMaintainer() != identifier_) continue;
      temoto_context_manager::ObjectContainer oc = getContainer<temoto_context_manager::ObjectContainer>(item_entry.first);
      publish_container_tf(type, oc);
    }
    else if (type == "MAP")
    {
      // If maintainer is another instance, don't publish
      if (getRosPayloadPtr<temoto_context_manager::MapContainer>(item_entry.first)->getMaintainer() != identifier_) continue;
      temoto_context_manager::MapContainer mc = getContainer<temoto_context_manager::MapContainer>(item_entry.first);
      publish_container_tf(type, mc);
    }
  }
}

std::vector<temoto_context_manager::ItemContainer> EmrRosInterface::updateEmr(
                  const std::vector<temoto_context_manager::ItemContainer>& items_to_add, 
                  bool update_time)
{
  
  // Keep track of failed add/update attempts
  std::vector<temoto_context_manager::ItemContainer> failed_items;
  for (const auto& item_container : items_to_add)
  {
    if (item_container.type == "OBJECT") 
    {
      // Deserialize into an temoto_context_manager::ObjectContainer object and add to EMR
      temoto_context_manager::ObjectContainer oc = 
        temoto_core::deserializeROSmsg<temoto_context_manager::ObjectContainer>(item_container.serialized_container);
      if (!addOrUpdateEmrItem(oc, "OBJECT", item_container, update_time)) failed_items.push_back(item_container);
    }
    else if (item_container.type == "MAP") 
    {
      // Deserialize into an temoto_context_manager::MapContainer object and add to EMR
      temoto_context_manager::MapContainer mc = 
        temoto_core::deserializeROSmsg<temoto_context_manager::MapContainer>(item_container.serialized_container);
      if (!addOrUpdateEmrItem(mc, "MAP", item_container, update_time)) failed_items.push_back(item_container);
    }
    else if (item_container.type == "COMPONENT") 
    {
      // Deserialize into an temoto_context_manager::ComponentContainer object and add to EMR
      temoto_context_manager::ComponentContainer cc = 
        temoto_core::deserializeROSmsg<temoto_context_manager::ComponentContainer>(item_container.serialized_container);
      if (!addOrUpdateEmrItem(cc, "COMPONENT", item_container, update_time)) failed_items.push_back(item_container);
    }
    else
    {
      // TEMOTO_ERROR_STREAM("Wrong type " << item_container.type.c_str() << "specified for EMR item");
      failed_items.push_back(item_container);
    }
  }

  return failed_items;
}

template <class Container>
bool EmrRosInterface::addOrUpdateEmrItem(
            const Container& container, 
            const std::string& container_type, 
            const temoto_context_manager::ItemContainer& ic,
            const bool update_time)
{
  RosPayload<Container> rospl = RosPayload<Container>(container);
  rospl.setType(container_type);
  std::string name = container.name;
  std::string parent = container.parent;

  // Check for empty name field
  // Move these to the context manager interface maybe? TBD
  if (name == "") 
  {
    ROS_ERROR_STREAM("Empty string not allowed as EMR item name!");
    return false;
  }
  // Check if the parent exists
  if ((!parent.empty()) && (!env_model_repository_.hasItem(parent))) 
  {
    ROS_ERROR_STREAM("No parent with name " << parent << " found in EMR!");
    return false;
  }
  
  // Check if the object has to be added or updated
  if (!env_model_repository_.hasItem(name)) 
  {
    // Add the new item
    rospl.setMaintainer(ic.maintainer);
    std::shared_ptr<RosPayload<Container>> plptr = std::make_shared<RosPayload<Container>>(rospl);
    env_model_repository_.addItem(name, parent, plptr);
  }
  else
  {
    if (rospl.getTime() > getRosPayloadPtr<Container>(name)->getTime()) 
    {
      // Update the item information
      if (update_time) rospl.updateTime();
      std::shared_ptr<RosPayload<Container>> plptr = std::make_shared<RosPayload<Container>>(rospl);
      env_model_repository_.updateItem(name, plptr);
      ROS_INFO_STREAM("Updated item: " << name);
    }
  }
  return true;
}

std::vector<temoto_context_manager::ItemContainer> EmrRosInterface::EmrToVector()
{
  std::vector<temoto_context_manager::ItemContainer> items;
  std::vector<std::shared_ptr<emr::Item>> root_items = env_model_repository_.getRootItems();
  for (const auto& item : root_items)
  {
    EmrToVectorHelper(*item, items);
  }
  return items;
}

void EmrRosInterface::EmrToVectorHelper(const emr::Item& currentItem, std::vector<temoto_context_manager::ItemContainer>& items)
{
  // Create empty container and fill it based on the payload
  temoto_context_manager::ItemContainer ic;
  
  ic.type = currentItem.getPayload()->getType();

  // Get the item payload as ROS msg
  // To do this we dynamically cast the base class to the appropriate
  //   derived class and call the getPayload() method
  if (ic.type == "OBJECT") 
  {
    std::shared_ptr<RosPayload<temoto_context_manager::ObjectContainer>> rospl = 
      std::dynamic_pointer_cast<RosPayload<temoto_context_manager::ObjectContainer>>(currentItem.getPayload());
    ic.serialized_container = temoto_core::serializeROSmsg(rospl->getPayload());
    ic.maintainer = rospl->getMaintainer();
    items.push_back(ic);

  }
  else if (ic.type == "MAP") 
  {
    std::shared_ptr<RosPayload<temoto_context_manager::MapContainer>> rospl = 
      std::dynamic_pointer_cast<RosPayload<temoto_context_manager::MapContainer>>(currentItem.getPayload());
    ic.serialized_container = temoto_core::serializeROSmsg(rospl->getPayload());
    ic.maintainer = rospl->getMaintainer();
    items.push_back(ic);

  }
  else if (ic.type == "COMPONENT") 
  {
    std::shared_ptr<RosPayload<temoto_context_manager::ComponentContainer>> rospl = 
      std::dynamic_pointer_cast<RosPayload<temoto_context_manager::ComponentContainer>>(currentItem.getPayload());
    ic.serialized_container = temoto_core::serializeROSmsg(rospl->getPayload());
    ic.maintainer = rospl->getMaintainer();
    items.push_back(ic);

  }
  else
  {
    // TEMOTO_ERROR_STREAM("Wrong type of container @ EmrToVectorHelper: " << ic.type);
    return;
  }

  std::vector<std::shared_ptr<emr::Item>> children = currentItem.getChildren();
  for (uint32_t i = 0; i < children.size(); i++)
  {
    EmrToVectorHelper(*children[i], items);
  }
}

/**
 * @brief Function to traverse and print out every item name in the tree
 * 
 * @param root 
 */
void EmrRosInterface::traverseEmr(const emr::Item& root)
{
  // TEMOTO_DEBUG_STREAM(root.getPayload()->getName());
  std::shared_ptr<RosPayload<temoto_context_manager::ObjectContainer>> msgptr = 
    std::dynamic_pointer_cast<RosPayload<temoto_context_manager::ObjectContainer>>(root.getPayload());
  // TEMOTO_DEBUG_STREAM("tag_id: " << msgptr->getPayload().tag_id);
  std::vector<std::shared_ptr<emr::Item>> children = root.getChildren();
  for (uint32_t i = 0; i < children.size(); i++)
  {
    traverseEmr(*children[i]);
  }
}

} // namespace emr_ros_interface