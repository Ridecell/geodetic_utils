#include <ros/ros.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/NavSatFix.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <geometry_msgs/TransformStamped.h>
#include <geodetic_utils/geodetic_conv.hpp>

bool g_is_sim;
geodetic_converter::GeodeticConverter g_geodetic_converter;
sensor_msgs::Imu g_latest_imu_msg;
bool g_got_imu;
ros::Publisher g_gps_pose_pub;
ros::Publisher g_gps_transform_pub;

void imu_callback(const sensor_msgs::ImuConstPtr& msg)
{
  g_latest_imu_msg = *msg;
  g_got_imu = true;
}

void gps_callback(const sensor_msgs::NavSatFixConstPtr& msg)
{
  if (!g_got_imu) {
    ROS_WARN_STREAM_THROTTLE(1, "No IMU data yet");
    return;
  }

  if (msg->status.status < sensor_msgs::NavSatStatus::STATUS_FIX) {
    ROS_WARN_STREAM_THROTTLE(1, "No GPS fix");
    return;
  }

  if (!g_geodetic_converter.isInitialised()) {
    ROS_WARN_STREAM_THROTTLE(1, "No GPS reference point set, not publishing");
    return;
  }

  double x, y, z;
  g_geodetic_converter.geodetic2Enu(msg->latitude, msg->longitude, msg->altitude, &x, &y, &z);

  // (NWU -> ENU) for simulation
  if (g_is_sim) {
    double aux = x;
    x = y;
    y = -aux;
    //z = z;
  }

  // Fill up pose message
  geometry_msgs::PoseWithCovarianceStampedPtr pose_msg(
      new geometry_msgs::PoseWithCovarianceStamped);
  pose_msg->header = msg->header;
  pose_msg->header.frame_id = "world";
  pose_msg->pose.pose.position.x = x;
  pose_msg->pose.pose.position.y = y;
  pose_msg->pose.pose.position.z = z;
  pose_msg->pose.pose.orientation = g_latest_imu_msg.orientation;

  pose_msg->pose.covariance.assign(0);  // by default

//  if (msg->position_covariance_type == sensor_msgs::NavSatFix::COVARIANCE_TYPE_KNOWN
//      || msg->position_covariance_type == sensor_msgs::NavSatFix::COVARIANCE_TYPE_APPROXIMATED) {
//    // fill in completely (TODO, diagonal for now)
//    pose_msg->pose.covariance[6 * 0 + 0] = msg->position_covariance[3 * 0 + 0];
//    pose_msg->pose.covariance[6 * 1 + 1] = msg->position_covariance[3 * 1 + 1];
//    pose_msg->pose.covariance[6 * 2 + 2] = msg->position_covariance[3 * 2 + 2];
//
//  } else if (msg->position_covariance_type
//      == sensor_msgs::NavSatFix::COVARIANCE_TYPE_DIAGONAL_KNOWN) {
//    // fill in diagonal
//    pose_msg->pose.covariance[6 * 0 + 0] = msg->position_covariance[3 * 0 + 0];
//    pose_msg->pose.covariance[6 * 1 + 1] = msg->position_covariance[3 * 1 + 1];
//    pose_msg->pose.covariance[6 * 2 + 2] = msg->position_covariance[3 * 2 + 2];
//
//  } else {
//     //unknown or otherwise (default value)
  pose_msg->pose.covariance[6 * 0 + 0] = 0.05;
  pose_msg->pose.covariance[6 * 1 + 1] = 0.05;
  pose_msg->pose.covariance[6 * 2 + 2] = 0.05;
  pose_msg->pose.covariance[6 * 3 + 3] = 0.05;
  pose_msg->pose.covariance[6 * 4 + 4] = 0.05;
  pose_msg->pose.covariance[6 * 5 + 5] = 0.05;
//  }

  g_gps_pose_pub.publish(pose_msg);

  // Fill up transform message
  geometry_msgs::TransformStampedPtr transform_msg(new geometry_msgs::TransformStamped);
  transform_msg->header = msg->header;
  transform_msg->header.frame_id = "world";
  transform_msg->transform.translation.x = x;
  transform_msg->transform.translation.y = y;
  transform_msg->transform.translation.z = z;
  transform_msg->transform.rotation = g_latest_imu_msg.orientation;

  g_gps_transform_pub.publish(transform_msg);
}

int main(int argc, char** argv)
{
  ros::init(argc, argv, "gps_to_pose_conversion_node");
  ros::NodeHandle nh;
  ros::NodeHandle pnh("~");

  g_got_imu = false;

  // Use different coordinate transform if using simulator
  if (!pnh.getParam("sim", g_is_sim)) {
    ROS_WARN("Could not fetch 'sim' param, defaulting to 'false'");
    g_is_sim = false;
  }

  // Wait until GPS reference parameters are initialized.
  double latitude, longitude, altitude;
  do {
    ROS_INFO("Waiting for GPS reference parameters...");
    if (nh.getParam("/gps_ref_latitude", latitude) && nh.getParam("/gps_ref_longitude", longitude)
        && nh.getParam("/gps_ref_altitude", altitude)) {
      g_geodetic_converter.initialiseReference(latitude, longitude, altitude);
    } else {
      ROS_INFO("GPS reference not ready yet, use set_gps_reference_node to set it");
      ros::Duration(0.5).sleep();  // sleep for half a second
    }
  } while (!g_geodetic_converter.isInitialised());

  // Show reference point
  double initial_latitude, initial_longitude, initial_altitude;
  g_geodetic_converter.getReference(&initial_latitude, &initial_longitude, &initial_altitude);
  ROS_INFO("GPS reference initialized correctly %f, %f, %f", initial_latitude, initial_longitude,
           initial_altitude);

  // Initialize publishers
  g_gps_pose_pub = nh.advertise < geometry_msgs::PoseWithCovarianceStamped > ("gps_pose", 1);
  g_gps_transform_pub = nh.advertise < geometry_msgs::TransformStamped > ("gps_transform", 1);

  // Subscribe to IMU and GPS fixes, and convert in GPS callback
  ros::Subscriber imu_sub = nh.subscribe("imu", 1, &imu_callback);
  ros::Subscriber gps_sub = nh.subscribe("gps", 1, &gps_callback);

  ros::spin();
}
