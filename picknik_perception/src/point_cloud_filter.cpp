/*********************************************************************
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2014, University of Colorado, Boulder
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Univ of CO, Boulder nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/

/* Author: Dave Coleman
 */

#include <picknik_perception/point_cloud_filter.h>

//#include <cmath>
//#include <moveit/pointcloud_filter/pointcloud_filter.h>
//#include <moveit/occupancy_map_monitor/occupancy_map_monitor.h>
#include <message_filters/subscriber.h>
#include <sensor_msgs/point_cloud2_iterator.h>
//#include <XmlRpcException.h>

// Parameter loading
#include <rviz_visual_tools/ros_param_utilities.h>

namespace picknik_perception
{

PointCloudFilter::PointCloudFilter(const boost::shared_ptr<tf::Transformer> &tf, const std::string& map_frame)
  : tf_(tf)
  , map_frame_(map_frame)
  , private_nh_("~")
  , max_range_(std::numeric_limits<double>::infinity())
  , point_subsample_(1)
  , point_cloud_subscriber_(NULL)
  , point_cloud_filter_(NULL)
{
}

PointCloudFilter::~PointCloudFilter()
{
  stopHelper();
}

bool PointCloudFilter::updateTransformCache(const std::string &target_frame, const ros::Time &target_time)
{
  transform_cache_.clear();
  if (transform_provider_callback_)
    return transform_provider_callback_(target_frame, target_time, transform_cache_);
  else
  {
    ROS_WARN_THROTTLE(1, "No callback provided for updating the transform cache for octomap updaters");
    return false;
  }
}
bool PointCloudFilter::initialize()
{
  const std::string parent_name = "point_cloud_filter"; // for namespacing logging messages
  rviz_visual_tools::getStringParameter(parent_name, private_nh_, "point_cloud_topic", point_cloud_topic_);
  rviz_visual_tools::getDoubleParameter(parent_name, private_nh_, "max_range", max_range_);
  rviz_visual_tools::getIntParameter(parent_name, private_nh_, "point_subsample", point_subsample_);
  rviz_visual_tools::getStringParameter(parent_name, private_nh_, "filtered_cloud_topic", filtered_cloud_topic_);
  rviz_visual_tools::getDoubleParameter(parent_name, private_nh_, "max_range", max_range_);

  shape_mask_.reset(new point_containment_filter::ShapeMask());
  shape_mask_->setTransformCallback(boost::bind(&PointCloudFilter::getShapeTransform, this, _1, _2));
  if (!filtered_cloud_topic_.empty())
    filtered_cloud_publisher_ = private_nh_.advertise<sensor_msgs::PointCloud2>(filtered_cloud_topic_, 10, false);
  else
  {
    ROS_ERROR_STREAM_NAMED("filter","Must specify filtered_cloud_topic");
    return false;
  }
  return true;
}

void PointCloudFilter::start()
{
  if (point_cloud_subscriber_)
    return;
  /* subscribe to point cloud topic using tf filter*/
  point_cloud_subscriber_ = new message_filters::Subscriber<sensor_msgs::PointCloud2>(root_nh_, point_cloud_topic_, 5);
  if (tf_ && !map_frame_.empty())
  {
    point_cloud_filter_ = new tf::MessageFilter<sensor_msgs::PointCloud2>(*point_cloud_subscriber_, *tf_, map_frame_, 5);
    point_cloud_filter_->registerCallback(boost::bind(&PointCloudFilter::cloudMsgCallback, this, _1));
    ROS_INFO("Listening to '%s' using message filter with target frame '%s'", point_cloud_topic_.c_str(), point_cloud_filter_->getTargetFramesString().c_str());
  }
  else
  {
    point_cloud_subscriber_->registerCallback(boost::bind(&PointCloudFilter::cloudMsgCallback, this, _1));
    ROS_INFO("Listening to '%s'", point_cloud_topic_.c_str());
  }
}

void PointCloudFilter::stopHelper()
{
  delete point_cloud_filter_;
  delete point_cloud_subscriber_;
}

void PointCloudFilter::stop()
{
  stopHelper();
  point_cloud_filter_ = NULL;
  point_cloud_subscriber_ = NULL;
}

ShapeHandle PointCloudFilter::excludeShape(const shapes::ShapeConstPtr &shape, const double &scale, const double &padding)
{
  ShapeHandle h = 0;
  if (shape_mask_)
    h = shape_mask_->addShape(shape, scale, padding);
  else
    ROS_ERROR("Shape filter not yet initialized!");
  return h;
}

void PointCloudFilter::forgetShape(ShapeHandle handle)
{
  if (shape_mask_)
    shape_mask_->removeShape(handle);
}

bool PointCloudFilter::getShapeTransform(ShapeHandle h, Eigen::Affine3d &transform) const
{
  ShapeTransformCache::const_iterator it = transform_cache_.find(h);
  if (it == transform_cache_.end())
  {
    ROS_ERROR("Internal error. Shape filter handle %u not found", h);
    return false;
  }
  transform = it->second;
  return true;
}

void PointCloudFilter::cloudMsgCallback(const sensor_msgs::PointCloud2::ConstPtr &cloud_msg)
{
  ROS_DEBUG("Received a new point cloud message");
  ros::WallTime start = ros::WallTime::now();

  if (map_frame_.empty())
    map_frame_ = cloud_msg->header.frame_id;

  /* get transform for cloud into map frame */
  tf::StampedTransform map_H_sensor;
  if (map_frame_ == cloud_msg->header.frame_id)
    map_H_sensor.setIdentity();
  else
  {
    if (tf_)
    {
      try
      {
        tf_->lookupTransform(map_frame_, cloud_msg->header.frame_id, cloud_msg->header.stamp, map_H_sensor);
      }
      catch (tf::TransformException& ex)
      {
        ROS_ERROR_STREAM("Transform error of sensor data: " << ex.what() << "; quitting callback");
        return;
      }
    }
    else
      return;
  }

  /* compute sensor origin in map frame */
  //const tf::Vector3 &sensor_origin_tf = map_H_sensor.getOrigin();
  Eigen::Vector3d sensor_origin_eigen; // unused - todo, remove
  // (sensor_origin_tf.getX(), sensor_origin_tf.getY(), sensor_origin_tf.getZ());

  if (!updateTransformCache(cloud_msg->header.frame_id, cloud_msg->header.stamp))
  {
    ROS_ERROR_THROTTLE(1, "Transform cache was not updated. Self-filtering may fail.");
    return;
  }

  /* mask out points on the robot */
  shape_mask_->maskContainment(*cloud_msg, sensor_origin_eigen, 0.0, max_range_, mask_);

  boost::scoped_ptr<sensor_msgs::PointCloud2> filtered_cloud;

  //We only use these iterators if we are creating a filtered_cloud for
  //publishing. We cannot default construct these, so we use scoped_ptr's
  //to defer construction
  boost::scoped_ptr<sensor_msgs::PointCloud2Iterator<float> > iter_filtered_x;
  boost::scoped_ptr<sensor_msgs::PointCloud2Iterator<float> > iter_filtered_y;
  boost::scoped_ptr<sensor_msgs::PointCloud2Iterator<float> > iter_filtered_z;

  filtered_cloud.reset(new sensor_msgs::PointCloud2());
  filtered_cloud->header = cloud_msg->header;
  {
    sensor_msgs::PointCloud2Modifier pcd_modifier(*filtered_cloud);
    pcd_modifier.setPointCloud2FieldsByString(1, "xyz");
    pcd_modifier.resize(cloud_msg->width * cloud_msg->height);
  }

  //we have created a filtered_out, so we can create the iterators now
  iter_filtered_x.reset(new sensor_msgs::PointCloud2Iterator<float>(*filtered_cloud, "x"));
  iter_filtered_y.reset(new sensor_msgs::PointCloud2Iterator<float>(*filtered_cloud, "y"));
  iter_filtered_z.reset(new sensor_msgs::PointCloud2Iterator<float>(*filtered_cloud, "z"));

  size_t filtered_cloud_size = 0;

  for(unsigned int row = 0; row < cloud_msg->height; row += point_subsample_)
  {
    unsigned int row_c = row * cloud_msg->width;
    sensor_msgs::PointCloud2ConstIterator<float> pt_iter(*cloud_msg, "x");
    //set iterator to point at start of the current row
    pt_iter += row_c;

    for (unsigned int col = 0; col < cloud_msg->width; col += point_subsample_,
           pt_iter += point_subsample_)
    {
      /* check for NaN */
      if (!isnan(pt_iter[0]) && !isnan(pt_iter[1]) && !isnan(pt_iter[2]))
      {
        /* transform to map frame */
        // TODO remove this line
        tf::Vector3 point_tf = map_H_sensor * tf::Vector3(pt_iter[0], pt_iter[1],
                                                          pt_iter[2]);

        /* occupied cell at ray endpoint if ray is shorter than max range and this point
           isn't on a part of the robot*/
        if (mask_[row_c + col] == point_containment_filter::ShapeMask::INSIDE)
        {}
        else if (mask_[row_c + col] == point_containment_filter::ShapeMask::CLIP)
        {}
        else
        {
          //build list of valid points if we want to publish them

          **iter_filtered_x = pt_iter[0];
          **iter_filtered_y = pt_iter[1];
          **iter_filtered_z = pt_iter[2];
          ++filtered_cloud_size;
          ++*iter_filtered_x;
          ++*iter_filtered_y;
          ++*iter_filtered_z;
        }
      }
    }
  }

  ROS_DEBUG("Processed point cloud in %lf ms", (ros::WallTime::now() - start).toSec() * 1000.0);
  {
    sensor_msgs::PointCloud2Modifier pcd_modifier(*filtered_cloud);
    pcd_modifier.resize(filtered_cloud_size);
    filtered_cloud_publisher_.publish(*filtered_cloud);
  }
}

}