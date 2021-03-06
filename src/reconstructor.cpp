/// Copyright 2015 Miquel Massot Campos
/// Systems, Robotics and Vision
/// University of the Balearic Islands
/// All rights reserved.

#include <laser_stripe_reconstruction/reconstructor.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>
#include <pcl_conversions/pcl_conversions.h>

/**
 * @brief Reconstructor constructor
 *
 * @param nh Global node handle
 * @param nhp Private node handle
 */
Reconstructor::Reconstructor(ros::NodeHandle nh,
                             ros::NodeHandle nhp)
                           : nh_(nh), nhp_(nhp), it_(nh) {
  nhp_.param("uwsim_detector", uwsim_, false);
  nhp_.param("camera_frame_id", camera_frame_id_, std::string("/camera"));

  camera_sub_ = it_.subscribeCamera("image",
                                    5,  // queue size
                                    &Reconstructor::imageCallback,
                                    this);  // transport

  point_cloud_pub_ = nhp_.advertise<PointCloud>("points2", 1);

  if (uwsim_) {
    uwsim_detector_  = new UWSimDetector(nh_, nhp_);
  } else {
    detector_  = new Detector(nh_, nhp_);
  }
  triangulator_ = new Triangulator(nh_, nhp_);
  calibrator_ = new Calibrator(nh_, nhp_);
  calibration_service_ = nhp_.advertiseService("calibrate", &Reconstructor::calibrate, this);
  nhp_.param("calibrate", calibration_, false);
}

/**
 * @brief Image callback
 *
 * @param image_msg ROS image message
 * @param info_msg ROS info message
 */
void Reconstructor::imageCallback(
    const sensor_msgs::ImageConstPtr      &image_msg,
    const sensor_msgs::CameraInfoConstPtr &info_msg) {
  // sanity check
  ros::Time stamp  = info_msg->header.stamp;
  //camera_frame_id_ = info_msg->header.frame_id;

  ROS_INFO_ONCE("Images are received...");

  if (stamp == ros::Time(0)) {
    ROS_INFO_THROTTLE(10.0,
      "[Reconstructor]: Waiting for topics to be published.");
    return;
  }

  cv_bridge::CvImageConstPtr cv_image_ptr;

  try {
    cv_image_ptr = cv_bridge::toCvShare(image_msg,
                                        sensor_msgs::image_encodings::BGR8);
  } catch (cv_bridge::Exception& e) {
    ROS_ERROR("cv_bridge exception: %s", e.what());
    return;
  }

  // If calibration is requested, detect and call calibration
  if (calibration_) {
    // Detect points in image
    std::vector<cv::Point2d> points2;
    if (uwsim_) {
      points2 = uwsim_detector_->detect(cv_image_ptr->image);
    } else {
      points2 = detector_->detect(cv_image_ptr->image);
    }
    // Set the camera info
    calibrator_->setCameraInfo(info_msg);
    if (calibrator_->detectChessboard(cv_image_ptr->image, points2)) {
      ROS_INFO("Frame added!");
    }
  }

  if (!calibration_) {
    // Detect points in image
    ROS_INFO("Detect");
    std::vector<cv::Point2d> points2;
    if (uwsim_) {
      points2 = uwsim_detector_->detect(cv_image_ptr->image);
    } else {
      points2 = detector_->detect(cv_image_ptr->image);
    }

    ROS_INFO("Triangulate");
    // Triangulate points in space
    std::vector<cv::Point3d> points3;
    triangulator_->setCameraInfo(info_msg);
    points3 = triangulator_->triangulate(points2);

    ROS_INFO_STREAM("Publish " << points3.size() << " points");
    if (points3.empty()) {
      // nothing
    } else {
      publishPoints(points3, stamp);
    }
  }
}

/**
 * @brief Pointcloud publisher callback
 *
 * @param points Input vector of 3D points to publish
 * @param stamp ROS time to publish the points
 */
void Reconstructor::publishPoints(
    const std::vector<cv::Point3d>& points,
    const ros::Time& stamp) {
  PointCloud::Ptr point_cloud(new PointCloud());

  std_msgs::Header header;
  header.stamp = stamp;
  header.frame_id = camera_frame_id_;
  pcl_conversions::toPCL(header, point_cloud->header);
  // point_cloud->header.frame_id = camera_frame_id_;
  point_cloud->width           = points.size();
  point_cloud->height          = 1;
  point_cloud->points.resize(points.size());

  for (size_t i = 0; i < points.size(); i++) {
    point_cloud->points[i].x = points[i].x;
    point_cloud->points[i].y = points[i].y;
    point_cloud->points[i].z = points[i].z;
  }

  point_cloud_pub_.publish(point_cloud);
}

bool Reconstructor::calibrate(std_srvs::Empty::Request&,
                              std_srvs::Empty::Response&) {
  calibration_ = true;
  return true;
}

