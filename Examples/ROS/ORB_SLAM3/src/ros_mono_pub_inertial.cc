/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/


#include<iostream>
#include<algorithm>
#include<fstream>
#include<chrono>
#include <time.h>

#include <image_transport/image_transport.h>

#include<ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include "sensor_msgs/PointCloud2.h"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/PoseArray.h"
#include "std_msgs/String.h"
#include <Eigen/Dense>
#include <opencv2/core/eigen.hpp>
#include <opencv2/opencv.hpp>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>

#include<opencv2/core/core.hpp>

#include"../../../include/System.h"

#include "MapPoint.h"
#include <opencv2/highgui/highgui_c.h>
#include <opencv2/highgui/highgui.hpp>

#include<vector>
#include<queue>
#include<thread>
#include<mutex>

#include<sensor_msgs/Imu.h>
#include<opencv2/core/core.hpp>
#include"../include/ImuTypes.h"

//! parameters
bool read_from_topic = false, read_from_camera = false, bEqual = false;

//for connecting to tello
//http://wiki.ros.org/tello_driver
//just by doing roslaunch, we can get tello to publish images? to the below topic?
std::string image_topic = "/tello/image_raw";
//std::string image_topic = "/camera/image_raw"; // change to /tello
int all_pts_pub_gap = 0;
bool show_viewer = true;


vector<cv::Mat> vFeaturedImages;//vector of images in the videos

vector<string> vstrImageFilenames;//name of the images files-its vector
vector<double> vTimestamps;//vector of timestamps
cv::VideoCapture cap_obj;//to capture the video a special object

bool pub_all_pts = false;
int pub_count = 0;

void LoadImages(const string &strSequence, vector<string> &vstrImageFilenames,
	vector<double> &vTimestamps);
inline bool isInteger(const std::string & s);
void publish(ORB_SLAM3::System &SLAM, ros::Publisher &pub_pts_and_pose,
	ros::Publisher &pub_all_kf_and_pts, int frame_id, cv::Mat &im);

image_transport::Publisher pub_image;//class for publishing images in ROS declared.

class ImuGrabber
{
public:
    ImuGrabber(){};
    void GrabImu(const sensor_msgs::ImuConstPtr &imu_msg);

    queue<sensor_msgs::ImuConstPtr> imuBuf;
    std::mutex mBufMutex;
};


class ImageGrabber{
public:
	ImageGrabber(ORB_SLAM3::System &_SLAM, ImuGrabber *_pImuGb, const bool _bClahe,ros::Publisher &_pub_pts_and_pose,
		ros::Publisher &_pub_all_kf_and_pts) :
		SLAM(_SLAM),mpImuGb(_pImuGb), mbClahe(_bClahe), pub_pts_and_pose(_pub_pts_and_pose),
		pub_all_kf_and_pts(_pub_all_kf_and_pts), frame_id(0){}

	void GrabImage(const sensor_msgs::ImageConstPtr& msg);
	cv::Mat GetImage(const sensor_msgs::ImageConstPtr &img_msg);
    void SyncWithImu();

	queue<sensor_msgs::ImageConstPtr> img0Buf;
    std::mutex mBufMutex;

	ORB_SLAM3::System &SLAM;
	ImuGrabber *mpImuGb;
    const bool mbClahe;
    cv::Ptr<cv::CLAHE> mClahe = cv::createCLAHE(3.0, cv::Size(8, 8));

	ros::Publisher &pub_pts_and_pose;
	ros::Publisher &pub_all_kf_and_pts;
	int frame_id;
};
bool parseParams(int argc, char **argv);

void printFunction(const std_msgs::String::ConstPtr& value) {
	printf("Received message from ros_tello.py at test/raw with value=%s\n", value->data.c_str());
}

void reportImage(const sensor_msgs::ImageConstPtr img) {
	printf("Got image with width %u and height %u\n", img->width, img->height);
}

using namespace std;

//recordings 



void LoadImagesTUMVI(const string &strImagePath, const string &strPathTimes,
                vector<string> &vstrImages, vector<double> &vTimeStamps);

void LoadIMU(const string &strImuPath, vector<double> &vTimeStamps, vector<cv::Point3f> &vAcc, vector<cv::Point3f> &vGyro);

int main(int argc, char **argv){
	ros::init(argc, argv, "Monopub");
	ros::start();
	ros::console::set_logger_level(ROSCONSOLE_DEFAULT_NAME, ros::console::levels::Info);
	if (!parseParams(argc, argv)) {
		return EXIT_FAILURE;
	}
	int n_images = vstrImageFilenames.size();
	//recording stuff
	





	// Create SLAM system. It initializes all system threads and gets ready to process frames.
	ORB_SLAM3::System SLAM(argv[1], argv[2], ORB_SLAM3::System::IMU_MONOCULAR, show_viewer);	
	
  
  	


	ros::NodeHandle nodeHandler;
	//ros::Publisher pub_cloud = nodeHandler.advertise<sensor_msgs::PointCloud2>("cloud_in", 1000);
	ros::Publisher pub_pts_and_pose = nodeHandler.advertise<geometry_msgs::PoseArray>("pts_and_pose", 1000);//pub_pts_pose is a ros publisher node that publishes on the given topic in "" and it publishes:https://github.com/IntelRealSense/librealsense/blob/master/third-party/realsense-file/rosbag/msgs/geometry_msgs/PoseArray.h
	ros::Publisher pub_all_kf_and_pts = nodeHandler.advertise<geometry_msgs::PoseArray>("all_kf_and_pts", 1000);
	image_transport::ImageTransport it(nodeHandler);
	pub_image = it.advertise("orb_camera/image", 1);//connection from image to ROS publisher node.
	if (read_from_topic) {
		ImuGrabber imugb;
		ImageGrabber igb(SLAM,&imugb,bEqual,pub_pts_and_pose, pub_all_kf_and_pts);
		// Maximum delay, 5 seconds
  	    ros::Subscriber sub_imu = nodeHandler.subscribe("/imu0", 1000, &ImuGrabber::GrabImu, &imugb); 
		ros::Subscriber sub = nodeHandler.subscribe(image_topic, 100, &ImageGrabber::GrabImage, &igb);
		ros::Subscriber subTest = nodeHandler.subscribe("test/raw", 10, printFunction);
		ros::Subscriber subImageTest = nodeHandler.subscribe("test/image", 10, reportImage);
		std::thread sync_thread(&ImageGrabber::SyncWithImu,&igb);
		//initialize a ROS subscriber node that subscribes to "image_topic", 
		//queue size =1 and upon receiving an image, calls Grab Image function, 
		//and passes to that function the igb object of Image grabber class 
		//with the given 2 publisher nodes defined above
		ros::spin();
	}
	else{
		ros::Rate loop_rate(5);
		cv::Mat im;
		double tframe = 0;
#ifdef COMPILEDWITHC11
		std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
#else
		std::chrono::monotonic_clock::time_point t1 = std::chrono::monotonic_clock::now();
#endif
		//cv::namedWindow("Press r to reset");
		int frame_id = 0;
		while (read_from_camera || frame_id < n_images){
			if (read_from_camera) {
				cap_obj.read(im);
#ifdef COMPILEDWITHC11
				std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
#else
				std::chrono::monotonic_clock::time_point t2 = std::chrono::monotonic_clock::now();
#endif
				tframe = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t1).count();
				//printf("fps: %f\n", 1.0 / tframe);
			}
			else {
				// Read image from file
				im = cv::imread(string(argv[3])+"/"+vstrImageFilenames[frame_id], cv::IMREAD_UNCHANGED);

				tframe = vTimestamps[frame_id];
			}
			if (im.empty()){
				cerr << endl << "Failed to load image at: " << vstrImageFilenames[frame_id] << endl;
				return 1;
			}
			// Pass the image to the SLAM system

			cv::Mat curr_pose = ORB_SLAM3::Converter::toCvMat(ORB_SLAM3::Converter::toSE3Quat(SLAM.TrackMonocular(im, tframe)));

			publish(SLAM, pub_pts_and_pose, pub_all_kf_and_pts, frame_id, im);

			++frame_id;

			//			int key = cv::waitKey(1);
			//			int key_mod = key % 256;
			//			if (key == 'r' || key_mod == 'r' || key == 'r' || key_mod == 'r') {
			//				printf("Resetting the SLAM system\n");
			//				SLAM.Shutdown();
			//				SLAM.reset(argv[2], show_viewer);
			//#ifdef COMPILEDWITHC11
			//				t1 = std::chrono::steady_clock::now();
			//#else
			//				t1 = std::chrono::monotonic_clock::now();
			//#endif
			//				frame_id = 0;
			//			}
			//cv::imshow("Press escape to exit", im);
			//if (cv::waitKey(1) == 27) {
			//	break;
			//}
			ros::spinOnce();
			loop_rate.sleep();
			if (!ros::ok()){ break; }
		}
	}
	//ros::spin();

	// Stop all threads
	SLAM.Shutdown();
	//geometry_msgs::PoseArray pt_array;
	//pt_array.header.seq = 0;
	//pub_pts_and_pose.publish(pt_array);
	ros::shutdown();
	return 0;
}

void publish(ORB_SLAM3::System &SLAM, ros::Publisher &pub_pts_and_pose,
	ros::Publisher &pub_all_kf_and_pts, int frame_id, cv::Mat &image) {
	if (all_pts_pub_gap > 0 && pub_count >= all_pts_pub_gap) {
		pub_all_pts = true;
		pub_count = 0;
	}
	if (pub_all_pts || SLAM.getLoopClosing()->mbLoopDetected || SLAM.getTracker()->loop_detected) {
		pub_all_pts = SLAM.getTracker()->loop_detected = SLAM.getLoopClosing()->mbLoopDetected = false;
		geometry_msgs::PoseArray kf_pt_array;
		vector<ORB_SLAM3::KeyFrame*> key_frames = SLAM.getMap()->GetAllKeyFrames();
		//! placeholder for number of keyframes
		kf_pt_array.poses.push_back(geometry_msgs::Pose());
		sort(key_frames.begin(), key_frames.end(), ORB_SLAM3::KeyFrame::lId);
		unsigned int n_kf = 0;
		unsigned int n_pts_id = 0;
		for (auto key_frame : key_frames) {
			// pKF->SetPose(pKF->GetPose()*Two);

			if (!key_frame || key_frame->isBad()) {
				printf("key frame is bad\n");
				continue;
			}

			cv::Mat R = ORB_SLAM3::Converter::toCvMat(key_frame->GetRotation()).t();
			vector<float> q = ORB_SLAM3::Converter::toQuaternion(R);
			cv::Mat twc = ORB_SLAM3::Converter::toCvMat(key_frame->GetCameraCenter());
			geometry_msgs::Pose kf_pose;

			kf_pose.position.x = twc.at<float>(0);
			kf_pose.position.y = twc.at<float>(1);
			kf_pose.position.z = twc.at<float>(2);
			kf_pose.orientation.x = q[0];
			kf_pose.orientation.y = q[1];
			kf_pose.orientation.z = q[2];
			kf_pose.orientation.w = q[3];
			kf_pt_array.poses.push_back(kf_pose);

			n_pts_id = kf_pt_array.poses.size();
			//! placeholder for number of points
			kf_pt_array.poses.push_back(geometry_msgs::Pose());
			std::set<ORB_SLAM3::MapPoint*> map_points = key_frame->GetMapPoints();
			unsigned int n_pts = 0;
			for (auto map_pt : map_points) {
				if (!map_pt || map_pt->isBad()) {
					printf("Point  is bad\n");
					continue;
				}
				cv::Mat pt_pose = ORB_SLAM3::Converter::toCvMat(map_pt->GetWorldPos());
				if (pt_pose.empty()) {
					printf("World position for point is empty\n");
					continue;
				}
				geometry_msgs::Pose curr_pt;
				//printf("wp size: %d, %d\n", wp.rows, wp.cols);
				//pcl_cloud->push_back(pcl::PointXYZ(wp.at<float>(0), wp.at<float>(1), wp.at<float>(2)));
				curr_pt.position.x = pt_pose.at<float>(0);
				curr_pt.position.y = pt_pose.at<float>(1);
				curr_pt.position.z = pt_pose.at<float>(2);
				kf_pt_array.poses.push_back(curr_pt);
				++n_pts;
			}
			kf_pt_array.poses[n_pts_id].position.x = (double)n_pts;
			kf_pt_array.poses[n_pts_id].position.y = (double)n_pts;
			kf_pt_array.poses[n_pts_id].position.z = (double)n_pts;
			++n_kf;
		}
		kf_pt_array.poses[0].position.x = (double)n_kf;
		kf_pt_array.poses[0].position.y = (double)n_kf;
		kf_pt_array.poses[0].position.z = (double)n_kf;
		kf_pt_array.header.frame_id = "/map";
		kf_pt_array.header.seq = frame_id + 1;
		printf("Publishing data for %u keyframes\n", n_kf);
		pub_all_kf_and_pts.publish(kf_pt_array);
	}
	else if (SLAM.getTracker()->mCurrentFrame.mbIsSet) { //not sure about mbIsSet
		++pub_count;
		SLAM.getTracker()->mCurrentFrame.mbIsSet = false;
		ORB_SLAM3::KeyFrame* pKF = SLAM.getTracker()->mCurrentFrame.mpReferenceKF;

		cv::Mat Trw = cv::Mat::eye(4, 4, CV_32F);

		// If the reference keyframe was culled, traverse the spanning tree to get a suitable keyframe.
		//while (pKF->isBad())
		//{
		//	Trw = Trw*pKF->mTcp;
		//	pKF = pKF->GetParent();
		//}

		vector<ORB_SLAM3::KeyFrame*> vpKFs = SLAM.getMap()->GetAllKeyFrames();
		sort(vpKFs.begin(), vpKFs.end(), ORB_SLAM3::KeyFrame::lId);

		// Transform all keyframes so that the first keyframe is at the origin.
		// After a loop closure the first keyframe might not be at the origin.
		     

		cv::Mat Two = ORB_SLAM3::Converter::toCvMat(ORB_SLAM3::Converter::toSE3Quat(vpKFs[0]->GetPoseInverse()));

		Trw = Trw* (ORB_SLAM3::Converter::toCvMat(ORB_SLAM3::Converter::toSE3Quat(pKF->GetPose())))*Two;
		cv::Mat lit = ORB_SLAM3::Converter::toCvMat(ORB_SLAM3::Converter::toSE3Quat(SLAM.getTracker()->mlRelativeFramePoses.back()));
		cv::Mat Tcw = lit*Trw;
		cv::Mat Rwc = Tcw.rowRange(0, 3).colRange(0, 3).t();
		cv::Mat twc = -Rwc*Tcw.rowRange(0, 3).col(3);

		vector<float> q = ORB_SLAM3::Converter::toQuaternion(Rwc);
		//geometry_msgs::Pose camera_pose;
		//std::vector<ORB_SLAM3::MapPoint*> map_points = SLAM.getMap()->GetAllMapPoints();
		std::vector<ORB_SLAM3::MapPoint*> map_points = SLAM.GetTrackedMapPoints();
		int n_map_pts = map_points.size();

		//printf("n_map_pts: %d\n", n_map_pts);

		//pcl::PointCloud<pcl::PointXYZ>::Ptr pcl_cloud(new pcl::PointCloud<pcl::PointXYZ>);

		geometry_msgs::PoseArray pt_array;
		//pt_array.poses.resize(n_map_pts + 1);

		geometry_msgs::Pose camera_pose;

		camera_pose.position.x = twc.at<float>(0);
		camera_pose.position.y = twc.at<float>(1);
		camera_pose.position.z = twc.at<float>(2);

		camera_pose.orientation.x = q[0];
		camera_pose.orientation.y = q[1];
		camera_pose.orientation.z = q[2];
		camera_pose.orientation.w = q[3];

		pt_array.poses.push_back(camera_pose);

		//printf("Done getting camera pose\n");

		for (int pt_id = 1; pt_id <= n_map_pts; ++pt_id){

			if (!map_points[pt_id - 1] || map_points[pt_id - 1]->isBad()) {
				//printf("Point %d is bad\n", pt_id);
				continue;
			}
			cv::Mat wp = ORB_SLAM3::Converter::toCvMat(map_points[pt_id - 1]->GetWorldPos());

			if (wp.empty()) {
				//printf("World position for point %d is empty\n", pt_id);
				continue;
			}
			geometry_msgs::Pose curr_pt;
			//printf("wp size: %d, %d\n", wp.rows, wp.cols);
			//pcl_cloud->push_back(pcl::PointXYZ(wp.at<float>(0), wp.at<float>(1), wp.at<float>(2)));
			curr_pt.position.x = wp.at<float>(0);
			curr_pt.position.y = wp.at<float>(1);
			curr_pt.position.z = wp.at<float>(2);
			pt_array.poses.push_back(curr_pt);
			//printf("Done getting map point %d\n", pt_id);
		}
		//sensor_msgs::PointCloud2 ros_cloud;
		//pcl::toROSMsg(*pcl_cloud, ros_cloud);
		//ros_cloud.header.frame_id = "1";
		//ros_cloud.header.seq = ni;

		// printf("frame_id: %d \n",  frame_id + 1);

		//printf("ros_cloud size: %d x %d\n", ros_cloud.height, ros_cloud.width);
		//pub_cloud.publish(ros_cloud);
		pt_array.header.frame_id = "/map";
		pt_array.header.seq = frame_id + 1;
		pub_pts_and_pose.publish(pt_array);

		// printf("valid map pts: %lu\n", pt_array.poses.size());

		if (pt_array.poses.size() > 250){
			printf("valid map pts: %lu\n", pt_array.poses.size());
			// cv::imshow("image", image);
			// cv::waitKey(1);
			// cv::destroyAllWindows();
			vFeaturedImages.push_back(image);

			cv::Mat & lastFeaturedImage = vFeaturedImages[vFeaturedImages.size() - 1];

			sensor_msgs::ImagePtr msg;
			msg = cv_bridge::CvImage(std_msgs::Header(), "mono8", lastFeaturedImage).toImageMsg();
			pub_image.publish(msg);
		}
	}
}

inline bool isInteger(const std::string & s){
	if (s.empty() || ((!isdigit(s[0])) && (s[0] != '-') && (s[0] != '+'))) return false;

	char * p;
	strtol(s.c_str(), &p, 10);

	return (*p == 0);
}

void LoadImages(const string &strPathToSequence, vector<string> &vstrImageFilenames, vector<double> &vTimestamps){
	
	
    	

	ifstream fTimes;
	string strPathTimeFile = strPathToSequence;
	fTimes.open(strPathTimeFile.c_str());
	// skip first three lines
    	string s0;
    	getline(fTimes,s0);
    	getline(fTimes,s0);
    	getline(fTimes,s0);

	while (!fTimes.eof()){
		string s;
		getline(fTimes, s);
		if (!s.empty()){
			stringstream ss;
			ss << s;
			double t;
			ss >> t;
			vTimestamps.push_back(t);
			string sRGB;
			ss >> sRGB;
             		vstrImageFilenames.push_back(sRGB);
		}
	}

	
}

void ImageGrabber::GrabImage(const sensor_msgs::ImageConstPtr &img_msg)
{
  mBufMutex.lock();
  if (!img0Buf.empty())
    img0Buf.pop();
  img0Buf.push(img_msg);
  mBufMutex.unlock();
}
cv::Mat ImageGrabber::GetImage(const sensor_msgs::ImageConstPtr &img_msg)
{
  // Copy the ros image message to cv::Mat.
  cv_bridge::CvImageConstPtr cv_ptr;
  try
  {
    cv_ptr = cv_bridge::toCvShare(img_msg, sensor_msgs::image_encodings::MONO8);
  }
  catch (cv_bridge::Exception& e)
  {
    ROS_ERROR("cv_bridge exception: %s", e.what());
  }
  
  if(cv_ptr->image.type()==0)
  {
    return cv_ptr->image.clone();
  }
  else
  {
    std::cout << "Error type" << std::endl;
    return cv_ptr->image.clone();
  }
  cv::Mat im = cv_ptr->image;
  publish(SLAM, pub_pts_and_pose, pub_all_kf_and_pts, frame_id, im);
  ++frame_id;

}

void ImageGrabber::SyncWithImu()
{ printf("sync with IMU called\n");
  while(1)
  {
    cv::Mat im;
    double tIm = 0;
    if (!img0Buf.empty()&&!mpImuGb->imuBuf.empty())
    {
      tIm = img0Buf.front()->header.stamp.toSec();
      if(tIm>mpImuGb->imuBuf.back()->header.stamp.toSec())
          continue;
      {
      this->mBufMutex.lock();
      im = GetImage(img0Buf.front());
      img0Buf.pop();
      this->mBufMutex.unlock();
      }

      vector<ORB_SLAM3::IMU::Point> vImuMeas;
      mpImuGb->mBufMutex.lock();
      if(!mpImuGb->imuBuf.empty())
      {
        // Load imu measurements from buffer
        vImuMeas.clear();
        while(!mpImuGb->imuBuf.empty() && mpImuGb->imuBuf.front()->header.stamp.toSec()<=tIm)
        {
          double t = mpImuGb->imuBuf.front()->header.stamp.toSec();
          cv::Point3f acc(mpImuGb->imuBuf.front()->linear_acceleration.x, mpImuGb->imuBuf.front()->linear_acceleration.y, mpImuGb->imuBuf.front()->linear_acceleration.z);
          cv::Point3f gyr(mpImuGb->imuBuf.front()->angular_velocity.x, mpImuGb->imuBuf.front()->angular_velocity.y, mpImuGb->imuBuf.front()->angular_velocity.z);
          vImuMeas.push_back(ORB_SLAM3::IMU::Point(acc,gyr,t));
          mpImuGb->imuBuf.pop();
        }
      }
      mpImuGb->mBufMutex.unlock();
      if(mbClahe)
        mClahe->apply(im,im);
	  printf("Calling IMU SLAM\n");
      SLAM.TrackMonocular(im,tIm,vImuMeas);
    }

    std::chrono::milliseconds tSleep(1);
    std::this_thread::sleep_for(tSleep);
  }
}

void ImuGrabber::GrabImu(const sensor_msgs::ImuConstPtr &imu_msg)
{ printf("imu reading in progress\n");
  mBufMutex.lock();
  imuBuf.push(imu_msg);
  mBufMutex.unlock();
  return;
}

bool parseParams(int argc, char **argv) {
	if (argc < 6){
		cerr << endl << "Usage: rosrun ORB_SLAM3 Monopub path_to_vocabulary path_to_settings path_to_sequence/camera_id/-1 <image_topic> [do_equalize]" << endl;
		return 1;
	}
	if (isInteger(std::string(argv[3]))) {
		int camera_id = atoi(argv[3]);
		if (camera_id >= 0){
			read_from_camera = true;
			printf("Reading images from camera with id %d\n", camera_id);
			cap_obj.open(camera_id);
			if (!(cap_obj.isOpened())) {
				printf("Camera stream could not be initialized successfully\n");
				ros::shutdown();
				return 0;
			}
			int img_height = cap_obj.get(cv::CAP_PROP_FRAME_HEIGHT);
			int img_width = cap_obj.get(cv::CAP_PROP_FRAME_WIDTH);
			printf("Images are of size: %d x %d\n", img_width, img_height);
		}
		else {
			read_from_topic = true;
			if (argc > 4){
				image_topic = std::string(argv[4]);
			}
			printf("Reading images from topic %s\n", image_topic.c_str());
		}
	}
	else {
		LoadImages(string(argv[3])+"/rgb.txt", vstrImageFilenames, vTimestamps);
	}
	int arg_id = 4;
	if (argc > arg_id) {
		all_pts_pub_gap = atoi(argv[arg_id++]);
	}
	if (argc > arg_id) {
		show_viewer = atoi(argv[arg_id++]);
	}
	printf("all_pts_pub_gap: %d\n", all_pts_pub_gap);
	printf("show_viewer: %d\n", show_viewer);
	//additional stuff for IMU - inertial SLAM
	if(argc==6)
   {
    std::string sbEqual(argv[5]);
    if(sbEqual == "true")
      bEqual = true;
   }
	return 1;
}






