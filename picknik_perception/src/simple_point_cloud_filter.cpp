/*********************************************************************
 * Software License Agreement ("Modified BSD License")
 *
 * Copyright (c) 2014, University of Colorado, Boulder
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 * * Neither the name of the Univ of CO, Boulder nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *********************************************************************/
/**
 * Authors : Andy Mcevoy
 * Desc    : Simple perception based on point cloud data
 */

#include <picknik_perception/simple_point_cloud_filter.h>

#include <shape_msgs/Mesh.h>
#include <shape_msgs/MeshTriangle.h>
#include <geometry_msgs/Point.h>

// PCL
#include <pcl/PCLPointCloud2.h>
#include <pcl/conversions.h>
#include <pcl/features/normal_3d.h>
#include <pcl/filters/passthrough.h>
#include <pcl/filters/radius_outlier_removal.h>
#include <pcl/filters/statistical_outlier_removal.h>
#include <pcl/io/pcd_io.h>
#include <pcl/io/ply_io.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/point_types.h>
#include <pcl/surface/gp3.h>

namespace picknik_perception
{

SimplePointCloudFilter::SimplePointCloudFilter(rviz_visual_tools::RvizVisualToolsPtr& visual_tools)
  : visual_tools_(visual_tools)
  , nh_("~")
  , has_roi_(false)
{
  processing_ = false;
  get_bbox_ = false;
  outlier_removal_ = false;

  // set regoin of interest
  roi_depth_ = 1.0;
  roi_width_ = 1.0;
  roi_height_ = 1.0;
  roi_pose_ = Eigen::Affine3d::Identity();

  // initialize cloud pointers
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr roi_cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
  roi_cloud_ = roi_cloud;

  // publish bin point cloud
  roi_cloud_pub_ = nh_.advertise<pcl::PointCloud<pcl::PointXYZRGB> >("roi_cloud",1);

  ROS_DEBUG_STREAM_NAMED("point_cloud_filter","Simple point cloud filter ready.");
}

shape_msgs::Mesh SimplePointCloudFilter::createPlyMsg(pcl::PointCloud<pcl::PointXYZRGB>::Ptr point_cloud)
{
  // from pcl docs:
  // http://pointclouds.org/documentation/tutorials/greedy_projection.php#greedy-triangulation

  // change cloud type
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::copyPointCloud(*point_cloud, *cloud);

  // compute normals estimation
  pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> normal_estimation;
  pcl::PointCloud<pcl::Normal>::Ptr normals(new pcl::PointCloud<pcl::Normal>);
  pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>);

  tree->setInputCloud(cloud);
  normal_estimation.setInputCloud(cloud);
  normal_estimation.setSearchMethod(tree);
  normal_estimation.setKSearch(20); // make smaller to capture more details (see pcl docs on normal estimation)
  normal_estimation.compute(*normals);

  // concatenate the XYZ and normal fields
  pcl::PointCloud<pcl::PointNormal>::Ptr cloud_with_normals(new pcl::PointCloud<pcl::PointNormal>);
  pcl::concatenateFields(*cloud, *normals, *cloud_with_normals);
  
  // create search tree
  pcl::search::KdTree<pcl::PointNormal>::Ptr tree2(new pcl::search::KdTree<pcl::PointNormal>);
  tree2->setInputCloud(cloud_with_normals);

  // initialize objects
  pcl::GreedyProjectionTriangulation<pcl::PointNormal> gp3;
  pcl::PolygonMesh triangles;

  gp3.setSearchRadius(0.025); // maximum distance betweeen connected points (max edge length)
  gp3.setMu(2.5);
  gp3.setMaximumNearestNeighbors(100);
  gp3.setMaximumSurfaceAngle(M_PI / 4.0);
  gp3.setMinimumAngle(M_PI / 18.0);
  gp3.setMaximumAngle(2.0 * M_PI / 3.0);
  gp3.setNormalConsistency(false);

  // get results
  gp3.setInputCloud(cloud_with_normals);
  gp3.setSearchMethod(tree2);
  gp3.reconstruct(triangles);

  // create mesh message
  std::size_t num_points = cloud_with_normals->size();
  std::size_t num_triangles = triangles.polygons.size();
  shape_msgs::Mesh mesh_msg;
  geometry_msgs::Point vertex;
  shape_msgs::MeshTriangle triangle;

  ROS_INFO_STREAM_NAMED("point_cloud_filter.plyMsg","created mesh with " << num_triangles << " triangles and " 
                        << num_points << " vertices");

  for (std::size_t i = 0; i < num_points; i++)
  {
    vertex.x = cloud_with_normals->points[i].x;
    vertex.y = cloud_with_normals->points[i].y;
    vertex.z = cloud_with_normals->points[i].z;
    
    mesh_msg.vertices.push_back(vertex);
  }

  for (std::size_t i = 0; i < num_triangles; i++)
  {
    for (std::size_t j = 0; j < 3; j++)
    {
      triangle.vertex_indices[j] = triangles.polygons[i].vertices[j];
    }
    mesh_msg.triangles.push_back(triangle);
  }

  ROS_INFO_STREAM_NAMED("point_cloud_filter.plyMsg","created mesh message  with " << mesh_msg.triangles.size() 
                        << " triangles and " << mesh_msg.vertices.size() << " vertices");

  return mesh_msg;
}

void SimplePointCloudFilter::createPlyFile(std::string file_name, pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud)
{
  
  std::string file_path = ros::package::getPath("picknik_perception");
  std::string full_path = file_path + "/data/" + file_name;

  ROS_INFO_STREAM_NAMED("point_cloud_filter.savePLY","saving point cloud to: " << full_path);

  if (cloud->size() == 0)
  {
    ROS_WARN_STREAM_NAMED("point_cloud_filter.savePLY","Point cloud has no points. Aborting.");
    return;
  }

  pcl::PLYWriter writer;
  pcl::PCLPointCloud2 cloud2_msg;
  pcl::toPCLPointCloud2(*cloud, cloud2_msg);

  // write to ply binary file
  writer.write(full_path, cloud2_msg, Eigen::Vector4f::Zero(), Eigen::Quaternionf::Identity(), true, true);
  ROS_INFO_STREAM_NAMED("point_cloud_filter.savePLY","Saved point cloud with " << cloud->size() << " points");
}


bool SimplePointCloudFilter::publishRegionOfInterest()
{
  if (!has_roi_)
  {
    ROS_ERROR_STREAM_NAMED("point_cloud_filter","No region of interest specified");
    return false;
  }
  // show region of interest
  visual_tools_->publishAxisLabeled(roi_pose_, "bin");
  visual_tools_->publishWireframeCuboid(roi_pose_, roi_depth_, roi_width_, roi_height_, rviz_visual_tools::CYAN);
}

void SimplePointCloudFilter::pointCloudCallback(const sensor_msgs::PointCloud2ConstPtr& msg)
{
  if (processing_)
  {
    ROS_ERROR_STREAM_NAMED("point_cloud_filter.pcCallback","skipped point cloud because currently busy");

    return;
  }

  processing_ = true;
  processPointCloud(msg);
  processing_ = false;
}

void SimplePointCloudFilter::processPointCloud(const sensor_msgs::PointCloud2ConstPtr& msg)
{
  pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
  pcl::fromROSMsg(*msg, *cloud);

  // Wait for first TF transform to arrive
  static const std::string BASE_LINK = "/world";
  //ROS_DEBUG_STREAM_NAMED("perception","Waiting for transform from " << BASE_LINK << " to " << cloud->header.frame_id);
  tf_listener_.waitForTransform(BASE_LINK, cloud->header.frame_id, msg->header.stamp, ros::Duration(2.0));

  if (!pcl_ros::transformPointCloud(BASE_LINK, *cloud, *roi_cloud_, tf_listener_))
  {
    ROS_ERROR_STREAM_NAMED("point_cloud_filter.process","Error converting to desired frame");
  }

  if (!has_roi_)
  {
    ROS_DEBUG_STREAM_THROTTLE_NAMED(2, "point_cloud_filter","No region of interest specified yet, showing all points");
  }
  else
  {

    // Filter based on bin location
    pcl::PassThrough<pcl::PointXYZRGB> pass_x;
    pass_x.setInputCloud(roi_cloud_);
    pass_x.setFilterFieldName("x");
    pass_x.setFilterLimits(roi_pose_.translation()[0]-roi_depth_ / 2.0, roi_pose_.translation()[0] + roi_depth_ / 2.0);
    pass_x.filter(*roi_cloud_);

    pcl::PassThrough<pcl::PointXYZRGB> pass_y;
    pass_y.setInputCloud(roi_cloud_);
    pass_y.setFilterFieldName("y");
    pass_y.setFilterLimits(roi_pose_.translation()[1] - roi_width_ / 2.0, roi_pose_.translation()[1] + roi_width_ / 2.0);
    pass_y.filter(*roi_cloud_);

    pcl::PassThrough<pcl::PointXYZRGB> pass_z;
    pass_z.setInputCloud(roi_cloud_);
    pass_z.setFilterFieldName("z");
    pass_z.setFilterLimits(roi_pose_.translation()[2] - roi_height_ / 2.0, roi_pose_.translation()[2] + roi_height_ / 2.0);
    pass_z.filter(*roi_cloud_);

    // slowish
    if (outlier_removal_)
    {
      ROS_WARN_STREAM_NAMED("simple_point_cloud_filter","Performing outlier removal");

      pcl::RadiusOutlierRemoval<pcl::PointXYZRGB> rad;
      rad.setInputCloud(roi_cloud_);
      rad.setRadiusSearch(0.03);
      rad.setMinNeighborsInRadius(200);
      rad.filter(*roi_cloud_);

      pcl::StatisticalOutlierRemoval<pcl::PointXYZRGB> sor;
      sor.setInputCloud(roi_cloud_);
      sor.setMeanK(50);
      sor.setStddevMulThresh(1.0);
      sor.filter(*roi_cloud_);
    }

    if (roi_cloud_->points.size() == 0)
    {
      ROS_WARN_STREAM_THROTTLE_NAMED(2, "point_cloud_filter.process","0 points left after filtering");
      return;
    }
  }

  // publish point clouds for rviz
  roi_cloud_pub_.publish(roi_cloud_);
  ROS_DEBUG_STREAM_THROTTLE_NAMED(2, "point_cloud_filter","Publishing filtered point cloud");

  // optionally get the bounding box of the point cloud
  if (get_bbox_)
  {
    bounding_box_.getBodyAlignedBoundingBox(roi_cloud_, bbox_pose_, bbox_depth_, bbox_width_, bbox_height_);

    // Visualize
    visual_tools_->publishWireframeCuboid(bbox_pose_, bbox_depth_, bbox_width_, bbox_height_,
                                          rviz_visual_tools::MAGENTA);

    get_bbox_ = false;
  }

}

void SimplePointCloudFilter::setRegionOfInterest(Eigen::Affine3d pose, double depth, double width, double height)
{
  roi_pose_ = pose;
  roi_depth_ = depth;
  roi_width_ = width;
  roi_height_ = height;
  has_roi_ = true;

  // Visualize
  publishRegionOfInterest();
}

void SimplePointCloudFilter::setRegionOfInterest(Eigen::Affine3d bottom_right_front_corner,
                                                 Eigen::Affine3d top_left_back_corner, double reduction_padding_x, double reduction_padding_y, double reduction_padding_z)
{
  Eigen::Vector3d delta = top_left_back_corner.translation() - bottom_right_front_corner.translation();
  roi_depth_ = std::abs(delta[0]) - reduction_padding_x * 2.0;
  roi_width_ = std::abs(delta[1]) - reduction_padding_y * 2.0;
  roi_height_ = std::abs(delta[2])- reduction_padding_z * 2.0;
  has_roi_ = true;

  roi_pose_ = bottom_right_front_corner;
  roi_pose_.translation() += Eigen::Vector3d(roi_depth_ / 2.0 + reduction_padding_x,
                                             roi_width_ / 2.0 + reduction_padding_y,
                                             roi_height_ / 2.0 + reduction_padding_z);

  // Visualize
  publishRegionOfInterest();
}

void SimplePointCloudFilter::resetRegionOfInterst()
{
  has_roi_ = false;
}

void SimplePointCloudFilter::enableBoundingBox(bool enable)
{
  get_bbox_ = enable;
}

void SimplePointCloudFilter::getObjectPose(geometry_msgs::Pose &pose)
{
  pose = visual_tools_->convertPose(bbox_pose_);
}

} // namespace
