/*
* Software License Agreement (BSD License)
*
*  Copyright (c) 2009, Willow Garage, Inc.
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
*   * Neither the name of Willow Garage, Inc. nor the names of its
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
*
* $Id: bag_to_pcd.cpp 35812 2011-02-08 00:05:03Z rusu $
*
*/

#include <boost/filesystem.hpp>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <pcl_conversions/pcl_conversions.h>
#include "pcl_ros/transforms.h"
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_types.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/conversions.h>

typedef sensor_msgs::PointCloud2 PointCloud;
typedef PointCloud::Ptr PointCloudPtr;
typedef PointCloud::ConstPtr PointCloudConstPtr;

/* ---[ */
int
  main (int argc, char** argv)
{
  ros::init (argc, argv, "bag_to_pcd");
  if (argc < 4)
  {
    std::cerr << "Syntax is: " << argv[0] << " <file_in.bag> <topic> <output_directory> [<target_frame>]" << std::endl;
    std::cerr << "Example: " << argv[0] << " data.bag /laser_tilt_cloud ./pointclouds /base_link" << std::endl;
    return (-1);
  }

  // TF
  tf::TransformListener tf_listener;
  tf::TransformBroadcaster tf_broadcaster;

  rosbag::Bag bag;
  rosbag::View view;
  rosbag::View::iterator view_it;

  try
  {
    bag.open (argv[1], rosbag::bagmode::Read);
  } 
  catch (const rosbag::BagException&) 
  {
    std::cerr << "Error opening file " << argv[1] << std::endl;
    return (-1);
  }

  // check that target topic exists in the bag file:
  rosbag::View topic_list_view(bag);
  std::string target_topic;
  std::map<std::string, std::string> topic_list;
  for(rosbag::ConnectionInfo const *ci: topic_list_view.getConnections() )
  {
      topic_list[ci->topic] = ci->datatype;
      if (ci->topic == argv[2])
      {
                  if (ci->datatype == std::string("sensor_msgs/PointCloud2"))
                  {
                          target_topic = std::string (argv[2]);
                          view.addQuery (bag, rosbag::TopicQuery (target_topic));
                  }
                  else
                  {
                          std::cerr << "Provided topic '" << argv[2] << "' is in the bag file, but is not of type sensor_msgs/PointCloud2 (type: " << ci->datatype << ")" << std::endl;
                  }
      }
  }
  if (target_topic.empty())
  {
      std::cerr << "Could not find a sensor_msgs/PointCloud2 type on topic '" << argv[2] << "' in bag file " << argv[1] << std::endl;
      std::cerr << "Topics found in the bag file:" << std::endl;
      for (std::map<std::string, std::string>::iterator it=topic_list.begin(); it!=topic_list.end(); ++it)
          std::cout << "    " << it->first << " (" << it->second << ")" << std::endl;
      return (-1);
  }

  view.addQuery (bag, rosbag::TypeQuery ("tf/tfMessage"));
  view.addQuery (bag, rosbag::TypeQuery ("tf2_msgs/TFMessage"));
  view_it = view.begin ();

  std::string output_dir = std::string (argv[3]);
  boost::filesystem::path outpath (output_dir);
  if (!boost::filesystem::exists (outpath))
  {
    if (!boost::filesystem::create_directories (outpath))
    {
      std::cerr << "Error creating directory " << output_dir << std::endl;
      return (-1);
    }
    std::cerr << "Creating directory " << output_dir << std::endl;
  }

  // Add the PointCloud2 handler
  std::cerr << "Saving recorded sensor_msgs::PointCloud2 messages on topic " << target_topic << " to " << output_dir << std::endl;

  PointCloud cloud_t;
  ros::Duration r (0.001);
  // Loop over the whole bag file
  while (view_it != view.end ())
  {
    // Handle TF messages first
    tf::tfMessage::ConstPtr tf = view_it->instantiate<tf::tfMessage> ();
    if (tf != NULL)
    {
      tf_broadcaster.sendTransform (tf->transforms);
      ros::spinOnce ();
      r.sleep ();
    }
    else
    {
      // Get the PointCloud2 message
      PointCloudConstPtr cloud = view_it->instantiate<PointCloud> ();
      if (cloud == NULL)
      {
        ++view_it;
        continue;
      }

      // If a target_frame was specified
      if(argc > 4)
      {
        // Transform it
        if (!pcl_ros::transformPointCloud (argv[4], *cloud, cloud_t, tf_listener))
        {
        ++view_it;
        continue;
        }
      }
      else
      {
        // Else, don't transform it
        cloud_t = *cloud;
      }

      std::cerr << "Got " << cloud_t.width * cloud_t.height << " data points in frame " << cloud_t.header.frame_id << " on topic " << view_it->getTopic() << " with the following fields: " << pcl::getFieldsList (cloud_t) << std::endl;

      std::stringstream ss;
      ss << output_dir << "/" << cloud_t.header.stamp << ".pcd";
      std::cerr << "Data saved to " << ss.str () << std::endl;

      // pcl::io::savePCDFile (ss.str (), cloud_t, Eigen::Vector4f::Zero (),
      //                       Eigen::Quaternionf::Identity (), true);

      pcl::PCLPointCloud2 pcl_pc2;
      pcl_conversions::toPCL(cloud_t,pcl_pc2);
      pcl::PointCloud<pcl::PointXYZI>::Ptr temp_cloud(new pcl::PointCloud<pcl::PointXYZI>);
      pcl::fromPCLPointCloud2(pcl_pc2,*temp_cloud);
      pcl::io::savePCDFile (ss.str (), *temp_cloud);

    }
    // Increment the iterator
    ++view_it;
  }

  return (0);
}