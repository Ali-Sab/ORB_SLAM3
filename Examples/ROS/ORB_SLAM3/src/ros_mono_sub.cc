#include<iostream>
#include<algorithm>
#include<fstream>
#include<chrono>

#include<sstream>

#include<ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include "sensor_msgs/PointCloud2.h"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/PoseWithCovarianceStamped.h"
#include "geometry_msgs/PoseArray.h"
#include "nav_msgs/OccupancyGrid.h"
#include "nav_msgs/Path.h"
#include "std_msgs/String.h"

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>

#include<opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>


#include <opencv2/highgui/highgui_c.h>
#include <opencv2/highgui/highgui.hpp>
#include <Converter.h>

// #include <boost/algorithm/string/split.hpp>

#include <move_base_msgs/MoveBaseAction.h>
#include <actionlib/client/simple_action_client.h>
#include <Eigen/Dense>
#include <tf/transform_broadcaster.h>
#include <queue>

#ifndef DISABLE_FLANN
#include <flann/flann.hpp>
typedef flann::Index<flann::L2<double> > FLANN;
typedef std::unique_ptr<FLANN> FLANN_;
typedef flann::Matrix<double> flannMatT;
typedef flann::Matrix<int> flannResultT;
typedef std::unique_ptr<flannMatT> flannMatT_;
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//typedef actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction> MoveBaseClient;


// parameters
//what do each of these parameters mean and how we should edit them
float scale_factor = 3;
//"Since the ORB SLAM coordinate locations are in units
//of meters, a finer grid resolution is obtained by multiplying all positions by
//a scaling factor which is chosen as the inverse of the desired resolution in
//m/cell. For instance, if a resolution of 10 cm/cell or 0.1 m/cell is needed, the
//scaling factor becomes 10."-If we increase these numbers, we should expect to see better results but slower?
float resize_factor = 5;
//resize factor is usually being set to 1 in examples, not relevant
float cloud_max_x = 10;
float cloud_min_x = -10.0;
float cloud_max_z = 16;
float cloud_min_z = -5;
float free_thresh = 0.55;
float occupied_thresh = 0.50; //to declare a cell as occupied or not
float thresh_diff = 0.01;
int visit_thresh = 0;
unsigned int use_local_counters = 0;
unsigned int use_gaussian_counters = 0;
bool use_boundary_detection = false;
bool use_height_thresholding = false;
int canny_thresh = 350;
bool show_camera_location = true;
unsigned int gaussian_kernel_size = 3;
int cam_radius = 3;
// no. of keyframes between successive goal messages that are published
unsigned int goal_gap = 20;
bool enable_goal_publishing = false;
//enable_goal_publishing is a setting for RVIZ-which is our navigation visualizer
//that when set=0, means user is selecting a destination for the robot

#ifndef DISABLE_FLANN
double normal_thresh_deg = 0;
bool use_plane_normals = false;
double normal_thresh_y_rad=0.0;
std::vector<double> normal_angle_y;
FLANN_ flann_index;
#endif

float grid_max_x, grid_min_x,grid_max_z, grid_min_z;
cv::Mat global_occupied_counter, global_visit_counter;
cv::Mat local_occupied_counter, local_visit_counter;
cv::Mat local_map_pt_mask;
cv::Mat grid_map, grid_map_int, grid_map_thresh, grid_map_thresh_resized;
cv::Mat grid_map_rgb;
cv::Mat gauss_kernel;
float norm_factor_x, norm_factor_z;
float norm_factor_x_us, norm_factor_z_us;
int h, w;
unsigned int n_kf_received;
bool loop_closure_being_processed = false;
ros::Publisher pub_grid_map, pub_grid_map_metadata;
ros::Publisher pub_goal, pub_goal_path;
ros::Publisher pub_initial_pose, pub_current_pose, pub_current_particles;
ros::Publisher pub_command;
nav_msgs::OccupancyGrid grid_map_msg;
Eigen::Matrix4d transform_mat;
geometry_msgs::PoseWithCovarianceStamped init_pose_stamped, curr_pose_stamped;
tf::StampedTransform odom_to_map_transform_stamped;
geometry_msgs::PoseStamped goal;
geometry_msgs::PoseWithCovariance init_pose, curr_pose;

nav_msgs::Path goal_path;

cv::Mat img_final;


//#ifdef COMPILEDWITHC11
//std::chrono::steady_clock::time_point start_time, end_time;
//#else
//std::chrono::monotonic_clock::time_point start_time, end_time;
//#endif
//bool got_start_time;

int int_pos_grid_x, int_pos_grid_z;
float kf_pos_x, kf_pos_z;
int kf_pos_grid_x, kf_pos_grid_z;
geometry_msgs::Point kf_location;
geometry_msgs::Quaternion kf_orientation;
unsigned int kf_id = 0;
unsigned int init_pose_id = 0, curr_pose_id = 0, curr_path_id = 0, goal_id = 0;

using namespace std;
using namespace cv;

// Search functions
vector<geometry_msgs::Point> BFS(int init_x, int init_y, int final_x, int final_y);
bool isValid(int valid_x, int valid_y);
void returnNextCommand(vector<geometry_msgs::Point>& path);
void generatePath(vector<geometry_msgs::Point>& path);
void printPointPath(vector<geometry_msgs::Point>& path);
void publishCommand(std::string command);
ros::Time next_command_time;

// ORBSLAM functions
void updateGridMap(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose);
void resetGridMap(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose);
void cloudCallback(const sensor_msgs::PointCloud2::ConstPtr& pt_cloud);
void kfCallback(const geometry_msgs::PoseStamped::ConstPtr& camera_pose);
void saveMap(unsigned int id = 0);
void imageCallback(const sensor_msgs::ImageConstPtr& msg);
void ptCallback(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose);
void goalCallback(const geometry_msgs::PoseStamped new_goal);
void initialPoseCallback(const geometry_msgs::PoseWithCovarianceStamped initial_pose);
void currentPoseCallback(const geometry_msgs::PoseWithCovarianceStamped curr_pose);
void loopClosingCallback(const geometry_msgs::PoseArray::ConstPtr& all_kf_and_pts);
void getMixMax(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose,
	geometry_msgs::Point& min_pt, geometry_msgs::Point& max_pt);
void processMapPt(const geometry_msgs::Point &curr_pt, cv::Mat &occupied, cv::Mat &visited, 
	cv::Mat &pt_mask, int kf_pos_grid_x, int kf_pos_grid_z, unsigned int id);
void processMapPts(const std::vector<geometry_msgs::Pose> &pts, unsigned int n_pts,
	unsigned int start_id, int kf_pos_grid_x, int kf_pos_grid_z);
void getGridMap();
void showGridMap(unsigned int id = 0);
void parseParams(int argc, char **argv);
void printParams();

int main(int argc, char **argv){
	ros::init(argc, argv, "Monosub");
	ros::start();

	////tell the action client that we want to spin a thread by default
	//MoveBaseClient ac("move_base", true);
	//move_base_msgs::MoveBaseGoal goal;
	////wait for the action server to come up
	//while (!ac.waitForServer(ros::Duration(5.0))){
	//	ROS_INFO("Waiting for the move_base action server to come up");
	//}
	////we'll send a goal to the robot to move 1 meter forward
	//goal.target_pose.header.frame_id = "base_link";

	parseParams(argc, argv);
	printParams();
    //grid map creation stuff, abstracting away for now.
#ifndef DISABLE_FLANN
	if (normal_thresh_deg > 0 && normal_thresh_deg <= 90) {
		use_plane_normals = true;
		// threshold for angle with y axis = 90 - angle with xz plane
		normal_thresh_y_rad = (90 - normal_thresh_deg)*M_PI / 180.0;
		printf("normal_thresh_y_rad: %f rad\n", normal_thresh_y_rad);
		flann_index.reset(new FLANN(flann::KDTreeIndexParams(6)));
	}
#endif
	grid_max_x = cloud_max_x*scale_factor;
	grid_min_x = cloud_min_x*scale_factor;
	grid_max_z = cloud_max_z*scale_factor;
	grid_min_z = cloud_min_z*scale_factor;
	printf("grid_max: %f, %f\t grid_min: %f, %f\n", grid_max_x, grid_max_z, grid_min_x, grid_min_z);

	double grid_res_x = grid_max_x - grid_min_x, grid_res_z = grid_max_z - grid_min_z;

	h = grid_res_z;
	w = grid_res_x;
	printf("grid_size: (%d, %d)\n", h, w);
	n_kf_received = 0;

	
	global_occupied_counter.create(h, w, CV_32FC1);
	global_visit_counter.create(h, w, CV_32FC1);
	global_occupied_counter.setTo(cv::Scalar(0));
	global_visit_counter.setTo(cv::Scalar(0));

	grid_map_msg.data.resize(h*w);
	grid_map_msg.info.width = w;
	grid_map_msg.info.height = h;
	grid_map_msg.info.resolution = 1.0/scale_factor;

	grid_map_int = cv::Mat(h, w, CV_8SC1, (char*)(grid_map_msg.data.data()));

	grid_map.create(h, w, CV_32FC1);
	grid_map_thresh.create(h, w, CV_8UC1);
	grid_map_thresh_resized.create(h*resize_factor, w*resize_factor, CV_8UC1);
	grid_map_rgb.create(h*resize_factor, w*resize_factor, CV_8UC3);
	printf("output_size: (%d, %d)\n", grid_map_thresh_resized.rows, grid_map_thresh_resized.cols);

	cv::Mat whiteMatrix(200, 200, CV_8UC3, Scalar(255, 255, 255));//Declaring a white matrix
   	cv::Point center(100, 100);//Declaring the center point
   	int radius = 50; //Declaring the radius
	cv::Scalar line_Color(255, 0, 0);//Color of the circle
	int thickness = 2;//thickens of the line
	cv::namedWindow("whiteMatrix");//Declaring a window to show the circle
	cv::circle(whiteMatrix, center,radius, line_Color, thickness);//Using circle()function to draw the line//
	cv::imshow("WhiteMatrix", whiteMatrix);//Showing the circle//

	local_occupied_counter.create(h, w, CV_32FC1);
	local_visit_counter.create(h, w, CV_32FC1);
	local_map_pt_mask.create(h, w, CV_8UC1);

	gauss_kernel = cv::getGaussianKernel(gaussian_kernel_size, -1);

	norm_factor_x_us = float(cloud_max_x - cloud_min_x - 1) / float(cloud_max_x - cloud_min_x);
	norm_factor_z_us = float(cloud_max_z - cloud_min_z - 1) / float(cloud_max_z - cloud_min_z);
	printf("norm_factor_x_us: %f\n", norm_factor_x_us);
	printf("norm_factor_z_us: %f\n", norm_factor_z_us);

	norm_factor_x = float(grid_res_x - 1) / float(grid_max_x - grid_min_x);
	norm_factor_z = float(grid_res_z - 1) / float(grid_max_z - grid_min_z);
	printf("norm_factor_x: %f\n", norm_factor_x);
	printf("norm_factor_z: %f\n", norm_factor_z);
    //grid map creation stuff above

    //ROS stuff
	ros::NodeHandle nodeHandler;
    //intialize the ros subsriber node that will subsribe to pose,key frame data published by mono_pub.cc-custom function not standard ROS stuff
	//upon receiving the data from the pub.cc, it calls the function ptCallback
	//pt call back does a number of things:
    //1.publishes the initial jey frame as the initial pose
	//2.publishes the goal key frames pose
	ros::Subscriber sub_pts_and_pose = nodeHandler.subscribe("pts_and_pose", 1000, ptCallback);
	//move base simple is a standard ROS node for navigating a moving robot-http://wiki.ros.org/move_base
	//refer to: http://wiki.ros.org/navigation/Tutorials/SendingSimpleGoals, for simple example on 
	//functionality of this stack
    //sub_goal is a ros node that lets u subscribe to the topic move_base_simple/goal and upon receiving data 
	//from the topic, call goalCallback function
	//when enable_goal_publishing = 1
	//the following happens
    //ptCallBack function publishes the initial pose and it publishes the goal pose after #goal_gap key frames
	//this automatically published goals are sent to goalCallback which then
	//tries to navigate from intial pose to goal pose

	//However in the case, when enable_goal_publishing = 0 
	//ptCallback function does not publish initial or goal pose, it
	//keeps publishing current pose information as usual-but does not publish the initial or goal poses
	//this is used by currentPoseCallBack to navigate from current pose to goal pose, where
	//goal is set manually in RVIZ-how exactly not sure but tried setting in RVIZ from gui and worked
	//when enable_goal_publishing = 0 , we are sure that goalCallBack in not issuing any commands,
	//so that works but what about the case where enable_goal_publishing = 1, and we set a goal in Rviz
	// what will happen?
	// I think goals being published by RVIZ are just ignored and goals coming from ptCallBack are
	//relayed across to goalCallback from where they make it to move_base_simple client?

	ros::Subscriber sub_goal = nodeHandler.subscribe("move_base_simple/goal", 1000, goalCallback);
	//initial pose topic is there for AMCL method to work for localization
	//this subscriber ros node calls initialPoseCallback to process the intial pose info received
	//it initializes stuff like starting angle etc, gets goals from goalcallBack
	ros::Subscriber sub_initial_pose = nodeHandler.subscribe("initialpose", 1000, initialPoseCallback);
	//takes current pose data published by pt call back function and tries to navigate from the 
	//current pose to goal pose using bfs
	ros::Subscriber sub_current_pose = nodeHandler.subscribe("robot_pose", 1000, currentPoseCallback);
	ros::Subscriber sub_all_kf_and_pts = nodeHandler.subscribe("all_kf_and_pts", 1000, loopClosingCallback);

	pub_grid_map = nodeHandler.advertise<nav_msgs::OccupancyGrid>("map", 1000);
	pub_grid_map_metadata = nodeHandler.advertise<nav_msgs::MapMetaData>("map_metadata", 1000);
	pub_current_pose = nodeHandler.advertise<geometry_msgs::PoseWithCovarianceStamped>("robot_pose", 1000);
	//actual publishing of initpose is taking place in ptCallback
	pub_initial_pose = nodeHandler.advertise<geometry_msgs::PoseWithCovarianceStamped>("initialpose", 1000, true);
	pub_goal_path = nodeHandler.advertise<nav_msgs::Path>("goal_path", 1000);
	pub_command = nodeHandler.advertise<std_msgs::String>("tello/command", 1000);
    
	//< run Monosub with enable_goal_publishing set to 1 for automatic goal setting and 0 for manual
    //goal selection in Rviz 
	//so if enable_goal_publishing  = 1, we do not select a goal in RVIZ.

	if (enable_goal_publishing) {
		pub_goal = nodeHandler.advertise<geometry_msgs::PoseStamped>("move_base_simple/goal", 1000);
		//.advertise returns a ROS publisher node-called pub_goal here, that allows you to publish on the topic: move_base_simple/goal
		pub_current_particles = nodeHandler.advertise<geometry_msgs::PoseArray>("particlecloud", 1000, true);
	}
	tf::TransformBroadcaster br;
	tf::Transform odom_to_map_transform;
	odom_to_map_transform.setOrigin(tf::Vector3(0.0, 0.0, 0.0));
	tf::Quaternion q;
	q.setRPY(0, 0, 0);
	odom_to_map_transform.setRotation(q);
	//br.sendTransform(tf::StampedTransform(odom_to_map_transform, ros::Time::now(), "base_footprint", "map"));
	ros::Time tf_time = ros::Time::now();
	//br.sendTransform(tf::StampedTransform(odom_to_map_transform, tf_time, "map", "base_footprint"));
	br.sendTransform(tf::StampedTransform(odom_to_map_transform, tf_time, "map", "odom"));

	//ros::Subscriber sub_cloud = nodeHandler.subscribe("cloud_in", 1000, cloudCallback);
	//ros::Subscriber sub_kf = nodeHandler.subscribe("camera_pose", 1000, kfCallback);
	// ros::Subscriber sub_image = nodeHandler.subscribe("/camera/image_raw", 1, imageCallback);

	cv::namedWindow("grid_map_thresh_resized", CV_WINDOW_NORMAL);
	cv::namedWindow("grid_map_msg", CV_WINDOW_NORMAL);



	ros::spin();
	ros::shutdown();
	cv::destroyAllWindows();
	// saveMap(); 

	return 0;
}

// Unused in testing
void imageCallback(const sensor_msgs::ImageConstPtr& msg)
{
  try
  {
    cv::imshow("view", cv_bridge::toCvShare(msg, "bgr8")->image);
    cv::waitKey(30);
  }
  catch (cv_bridge::Exception& e)
  {
    ROS_ERROR("Could not convert from '%s' to 'bgr8'.", msg->encoding.c_str());
  }
}

void cloudCallback(const sensor_msgs::PointCloud2::ConstPtr& pt_cloud){
	ROS_INFO("I heard: [%s]{%d}", pt_cloud->header.frame_id.c_str(),
		pt_cloud->header.seq);
}
void kfCallback(const geometry_msgs::PoseStamped::ConstPtr& camera_pose){
	ROS_INFO("I heard: [%s]{%d}", camera_pose->header.frame_id.c_str(),
		camera_pose->header.seq);
}
void saveMap(unsigned int id) {
	std::string map_name_template = cv::format("grid_map_f%.2f_o%.2f_l%d_v%d_g%d_b%d_h%d_n%d_c%d", free_thresh, occupied_thresh, use_local_counters,
		visit_thresh, use_gaussian_counters, use_boundary_detection, use_height_thresholding, int(normal_thresh_deg), canny_thresh);
	printf("saving maps with id: %u and name template: %s\n", id, map_name_template.c_str());
	if (id > 0) {
		cv::imwrite("grid_maps//" + map_name_template + "_" + to_string(id) + ".jpg", grid_map);
		cv::imwrite("grid_maps//" + map_name_template + "_thresh_" + to_string(id) + ".jpg", grid_map_thresh);
		cv::imwrite("grid_maps//" + map_name_template + "_thresh_resized" + to_string(id) + ".jpg", grid_map_thresh_resized);
	}
	else {
		cv::imwrite("grid_maps//" + map_name_template + ".jpg", grid_map);
		cv::imwrite("grid_maps//" + map_name_template + ".jpg", grid_map_thresh);
		cv::imwrite("grid_maps//" + map_name_template + ".jpg", grid_map_thresh_resized);
	}

}

void initialPoseCallback(const geometry_msgs::PoseWithCovarianceStamped initial_pose) {
	// ROS_INFO("DFS position float: (%f, %f)\n", init_pose.pose.pose.position.x, init_pose.pose.pose.position.y);


	init_pose = initial_pose.pose;

	float pt_pos_x = init_pose.pose.position.x*scale_factor;
	float pt_pos_z = init_pose.pose.position.y*scale_factor;

	int_pos_grid_x = int(floor((pt_pos_x) * norm_factor_x));
	int_pos_grid_z = int(floor((pt_pos_z) * norm_factor_z));

	ROS_INFO("DFS initial position index: (%i, %i)\n", int_pos_grid_x, int_pos_grid_z);

	double currentAngle = tf::getYaw(init_pose.pose.orientation);
	cout << "Current Angle: "<< currentAngle;
	// tfScalar yaw, pitch, roll;
	// tf::Matrix3x3 mat(q);
	// mat.getEulerYPR(&yaw, &pitch, &roll);

	float goal_pos_x =  goal.pose.position.x*scale_factor;
	float goal_pos_z =  goal.pose.position.y*scale_factor;


	int kf_goal_pos_x = int(floor((goal_pos_x) * norm_factor_x));
	int kf_goal_pos_z = int(floor((goal_pos_z) * norm_factor_z));

	ROS_INFO("DFS goal index: (%i, %i)\n", kf_goal_pos_x, kf_goal_pos_z);

}

void currentPoseCallback(const geometry_msgs::PoseWithCovarianceStamped current_pose) {
	curr_pose = current_pose.pose;

	float pt_pos_x = curr_pose.pose.position.x*scale_factor;
	float pt_pos_z = curr_pose.pose.position.y*scale_factor;

	int_pos_grid_x = int(floor((pt_pos_x) * norm_factor_x));
	int_pos_grid_z = int(floor((pt_pos_z) * norm_factor_z));

	cout << "Current index: " << int_pos_grid_x << ", " << int_pos_grid_z << endl;
	double currentAngle = tf::getYaw(curr_pose.pose.orientation);
	cout << "Current Angle: "<< currentAngle;

	float goal_pos_x =  goal.pose.position.x*scale_factor;
	float goal_pos_z =  goal.pose.position.y*scale_factor;

	int kf_goal_pos_x = int(floor((goal_pos_x) * norm_factor_x));
	int kf_goal_pos_z = int(floor((goal_pos_z) * norm_factor_z));


	if ((kf_goal_pos_x < 0 || kf_goal_pos_x >= w) || (kf_goal_pos_z < 0 || kf_goal_pos_z >= h)) 
	{
		ROS_INFO("Invalid Indices: (%i, %i)\n", kf_goal_pos_x, kf_goal_pos_z);
		return;
	}

	if (kf_goal_pos_x == 0  && kf_goal_pos_z == 0){
		cout << "No goal currently set" << endl;
		return;
	}

	vector<geometry_msgs::Point> BFSpath = BFS(int_pos_grid_x, int_pos_grid_z, kf_goal_pos_x, kf_goal_pos_z);

	printPointPath(BFSpath);

	generatePath(BFSpath);

    returnNextCommand(BFSpath);

}

void goalCallback(const geometry_msgs::PoseStamped new_goal){
	//this "goal" is what gets sent to the move_base_simple/goal node
	//this goal here is being set using the new_goal
	//where is new_goal coming from?
	//new_goal is coming from the ptCallBack function.......
	//which publishes when enable_goal_publishing = 1
	goal.pose = new_goal.pose;

	// ROS_INFO("current DFS pose: (%i, %i)\n", kf_pos_grid_x, kf_pos_grid_z);
	// cv::Size s = grid_map.size();
	// ROS_INFO("current map size: (%i, %i)\n", s.height, s.width);

	// ROS_INFO("current map value: (%f)\n", grid_map.at<float>(kf_pos_grid_x, kf_pos_grid_z));

	float goal_pos_x =  goal.pose.position.x*scale_factor;
	float goal_pos_z =  goal.pose.position.y*scale_factor;


	
	int kf_goal_pos_x = int(floor((goal_pos_x) * norm_factor_x));
	int kf_goal_pos_z = int(floor((goal_pos_z) * norm_factor_z));

	ROS_INFO("DFS goal index: (%i, %i)\n", kf_goal_pos_x, kf_goal_pos_z);

	if ((kf_goal_pos_x < 0 || kf_goal_pos_x >= w) || (kf_goal_pos_z < 0 || kf_goal_pos_z >= h)) 
	{
		ROS_INFO("Invalid Indices: (%i, %i)\n", kf_goal_pos_x, kf_goal_pos_z);
		return;
	}
	//this function generates the list of commands for the bot to go the new goal
	// vector<geometry_msgs::Point> BFSpath =  BFS(kf_pos_grid_x, kf_pos_grid_z, kf_goal_pos_x, kf_goal_pos_z);
	vector<geometry_msgs::Point> BFSpath = BFS(int_pos_grid_x, int_pos_grid_z, kf_goal_pos_x, kf_goal_pos_z);

	printPointPath(BFSpath);

	generatePath(BFSpath);

    returnNextCommand(BFSpath);


	// ROS_INFO("current map value: (%f)\n", grid_map.at<float>(kf_goal_pos_x, kf_goal_pos_z));
	// ROS_INFO("DFS goal changed!: (%f, %f)\n", goal.pose.position.x, goal.pose.position.y);
}

vector<geometry_msgs::Point>  BFS(int init_x, int init_y, int final_x, int final_y){
	// int MIN_PATH_SIZE = 5;
	int MAX_OCCUPIED_PROB = 75;

	// These arrays are used to get row and column 
	// numbers of 4 neighbours of a given cell 
	int rowNum[] = {-1, 0, 0, 1}; 
	int colNum[] = {0, -1, 1, 0};

	ROS_INFO("Start/end indexes: (%i, %i) end (%i, %i)\n", init_x, init_y,final_x, final_y );


	cv::Mat test_grid_map_int = cv::Mat(h, w, CV_16SC1, (char*)(grid_map_msg.data.data()));
	// cv::Mat test_grid_map_int;

    cv::Mat img_first;


	double minval,maxval;
	cv::minMaxLoc(grid_map_int, &minval, &maxval, NULL, NULL);


	// cout << grid_map_int.type() << endl; // 1

	// cout << grid_map_int.rowRange(final_y, init_y) << endl;

	int erodeSize = 1;

	grid_map_int.convertTo(img_first, CV_16SC1);

	cv::Mat element = cv::getStructuringElement( cv::MORPH_RECT,
								cv::Size( 2*erodeSize + 1, 2*erodeSize+1 ),
								cv::Point( erodeSize, erodeSize ) );

	cv::erode(img_first, img_final, element);

	////////////////////////////////////
    vector<geometry_msgs::Point> path; // Store path history
	std::queue<vector<geometry_msgs::Point> > q;  // BFS queue
	cv::Mat visited;
	visited.create(h, w, CV_32FC1);
	visited.setTo(cv::Scalar(0));
	
	visited.at<int>(init_x, init_y) = 1;

	// Distance of source cell is 0 
	geometry_msgs::Point s; 
	s.x = init_x;
	s.y = init_y;
    path.push_back(s); 	
	q.push(path); // Enqueue source cell 

	while (!q.empty()) 
	{ 
        path = q.front();


		geometry_msgs::Point pt = path[path.size() - 1]; 

        if (pt.x == final_x && pt.y == final_y ) 
        {
            return path; 
        }

        q.pop();

		for (int i = 0; i < 4; i++) 
		{ 
			int col = pt.x + rowNum[i]; 
			int row = pt.y + colNum[i]; 

			int probability = (int)img_final.at<short>(row, col);
			if (isValid(row, col) 
				&& probability < MAX_OCCUPIED_PROB 
				&& probability >= 0 
				&& visited.at<int>(row, col) != 1)
			{ 
				// mark cell as visited and enqueue it 
				visited.at<int>(row, col) = 1;
				geometry_msgs::Point newPoint;
				newPoint.x = col;
				newPoint.y = row;

                vector<geometry_msgs::Point> newpath(path);
                newpath.push_back(newPoint); 

				q.push(newpath); 
			} 
		} 
	} 

	// Return blank vector if destination cannot be reached 
    // vector<geometry_msgs::Point> noPath; 
	return path; 	 
}

bool isValid(int valid_x, int valid_y) {
	if (valid_x < 0 || valid_x >= w)
		return false;

	if (valid_y < 0 || valid_y >= h)
		return false;

	return true;
}

void printPointPath(vector<geometry_msgs::Point>& path) 
{ 
    int size = path.size(); 
	cout << "Path of size " << size << ":" << endl; 

    for (int i = 0; i < size; i++)  {
		cout << path[i].x << "," << path[i].y;    
		// int probability = (int)grid_map_int.at<char>(path[i].y, path[i].x );
		int probability = (int)img_final.at<short>(path[i].y, path[i].x);

		cout << " occ%: " << probability <<  endl;    
	}
    cout << endl; 
} 

void generatePath(vector<geometry_msgs::Point>& path) 
{ 
    int size = path.size();
	// cout << "World frame path of size " << size << " generated" << endl; 

	nav_msgs::Path local_goal_path;
    for (int i = 0; i < size; i++) {
		geometry_msgs::PoseStamped path_pose_stamped;
		path_pose_stamped.header.frame_id = "map";
		path_pose_stamped.header.stamp = ros::Time::now();
		path_pose_stamped.header.seq = i;


		path_pose_stamped.pose.position.x = float((path[i].x) / (norm_factor_x * scale_factor));
		// path_pose_stamped.pose.y = 	0
		path_pose_stamped.pose.position.y = float((path[i].y) / (norm_factor_z * scale_factor));

		// cout << path[i].x << "," << path[i].y << " == ";     
		// cout << path_pose_stamped.pose.position.x << "," << path_pose_stamped.pose.position.y  << endl;     

		// path_pose_stamped.pose.w = 	0
		local_goal_path.poses.push_back(path_pose_stamped);
	}


	goal_path.header.frame_id = "map";
	goal_path.header.stamp = ros::Time::now();
	goal_path.header.seq = ++curr_path_id;
	goal_path.poses = local_goal_path.poses;
	pub_goal_path.publish(goal_path);
} 

void returnNextCommand(vector<geometry_msgs::Point>& path)
{
	// For now: manually set 2D pose est and 2d nav goal
	// cout << "X_indices: " << path[1].x << "," << path[0].x << endl;   
	// cout << "Y_indices: " << path[1].y << "," << path[0].y << endl;   
	
	int x_diff =  path[1].x - path[0].x;
	int y_diff =  path[1].y - path[0].y;
	
	// cout << "First and Second: ";
	// cout << "x_diff: " << x_diff << ", y_diff: " << y_diff << endl;

	float pt_pos_x = curr_pose.pose.position.x;
	float pt_pos_z = curr_pose.pose.position.y;

	// curr_pose.pose.orientation.x = kf_orientation.x;
	// curr_pose.pose.orientation.y = kf_orientation.z;
	// curr_pose.pose.orientation.z = kf_orientation.y;
	// curr_pose.pose.orientation.w = -kf_orientation.w;


	double currentAngle = tf::getYaw(curr_pose.pose.orientation);


	double desiredAngle = currentAngle;
	if(y_diff == 1){
		desiredAngle = M_PI / 2;
	} 
	else if(x_diff == 1){
		desiredAngle = 0;
	} else if(x_diff == -1){
		desiredAngle = M_PI;
	} else if(y_diff == -1){
		desiredAngle = - M_PI / 2;
	}
    // cout << "Current Angle: "<< currentAngle << "Desired Angle: "<< desiredAngle << " ." << endl;

	// CCW angle is positive 
	int AngleDiff = int((desiredAngle - currentAngle) * 180 / M_PI);

	// cout << "angle_diff: " << AngleDiff << endl; 

	AngleDiff -= 360. * std::floor((AngleDiff + 180.) * (1. / 360.));

	cout << "angle_diff wrap: " << AngleDiff << endl; 
	cout << "path size: " << path.size() << endl; 

	if(ros::Time::now() > next_command_time){
		cout << ros::Time::now() << endl;

		if(path.size() < 6) {
			publishCommand("land");
		}
		else if (AngleDiff >= 90) {
			publishCommand("ccw");
		} else if (AngleDiff <= -90) {
			publishCommand("cw");
		} else {
			publishCommand("forward");
		}
	}



	float world_x = (path[1].x) / (norm_factor_x * scale_factor);
	float world_y = (path[1].y) / (norm_factor_z * scale_factor);

	// cout << "init_pose:" << pt_pos_x << ", " << pt_pos_z << endl;
	// cout << "final_pose:" << world_x << ", " << world_y << endl;
}


// void sendCommandsInOrder(vector<std_msgs::String> cmds) {
	
// }

void publishCommand(std::string command){
	std_msgs::String msg;
	msg.data = command;
	cout << "Publish Command: " << command << endl;
	pub_command.publish(msg);

	next_command_time = ros::Time::now() + ros::Duration(5);
}

void ptCallback(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose){
	//ROS_INFO("Received points and pose: [%s]{%d}", pts_and_pose->header.frame_id.c_str(),
	//	pts_and_pose->header.seq);
	//if (pts_and_pose->header.seq==0) {
	//	cv::destroyAllWindows();
	//	saveMap();
	//	printf("Received exit message\n");
	//	ros::shutdown();
	//	exit(0);
	//}
//	if (!got_start_time) {
//#ifdef COMPILEDWITHC11
//		start_time = std::chrono::steady_clock::now();
//#else
//		start_time = std::chrono::monotonic_clock::now();
//#endif
//		got_start_time = true;
//	}
	if (loop_closure_being_processed){ return; }

	updateGridMap(pts_and_pose); //use the info from publisher to construct the grid map

	tf::TransformBroadcaster br;
	tf::Transform odom_to_map_transform;
	odom_to_map_transform.setOrigin(tf::Vector3(0.0, 0.0, 0.0));
	tf::Quaternion q;
	q.setRPY(0, 0, 0);
	odom_to_map_transform.setRotation(q);
	//br.sendTransform(tf::StampedTransform(odom_to_map_transform, ros::Time::now(), "base_footprint", "map"));
	ros::Time tf_time = ros::Time::now();
    //br.sendTransform(tf::StampedTransform(odom_to_map_transform, tf_time, "map", "base_footprint"));
	br.sendTransform(tf::StampedTransform(odom_to_map_transform, tf_time, "map", "odom"));

//#ifdef COMPILEDWITHC11
//	end_time = std::chrono::steady_clock::now();
//#else
//	end_time = std::chrono::monotonic_clock::now();
//#endif
//	double curr_time = std::chrono::duration_cast<std::chrono::duration<double>>(start_time - end_time).count();

	grid_map_msg.info.map_load_time = ros::Time::now();
	float kf_pos_grid_x_us = (kf_location.x - cloud_min_x) ;
	// float kf_pos_grid_x_us = (kf_location.x) * norm_factor_x_us;
	float kf_pos_grid_z_us = (kf_location.z - cloud_min_z) ;
	// float kf_pos_grid_z_us = (kf_location.z) * norm_factor_z_us;

    //current pose's x and y is set here
	curr_pose.pose.position.x = kf_pos_grid_x_us;
	curr_pose.pose.position.y = kf_pos_grid_z_us;

	// curr_pose.pose.position.x  = kf_pos_grid_x*resize_factor;
	// curr_pose.pose.position.y = kf_pos_grid_z*resize_factor;
	ROS_INFO("Publishing current pose: (%f, %f)\n", kf_pos_grid_x_us, kf_pos_grid_z_us);
	ROS_INFO("Publishing current pose: (%f, %f)\n", kf_location.x , kf_location.z);
	ROS_INFO("Publishing new current pose: (%f, %f)\n", kf_pos_grid_x*resize_factor, kf_pos_grid_z*resize_factor);
	curr_pose.pose.position.z = 0;
	// curr_pose.pose.orientation = kf_orientation;
	curr_pose.pose.orientation.x = kf_orientation.x;
	curr_pose.pose.orientation.y = kf_orientation.z;
	curr_pose.pose.orientation.z = kf_orientation.y;
	curr_pose.pose.orientation.w = -kf_orientation.w;
	cv::Mat(6, 6, CV_64FC1, curr_pose.covariance.elems).setTo(0);
	curr_pose_stamped.header.frame_id = "map";
	curr_pose_stamped.header.stamp = ros::Time::now();
	curr_pose_stamped.header.seq = ++curr_pose_id;
	curr_pose_stamped.pose = curr_pose;

	pub_current_pose.publish(curr_pose_stamped);//used by current pose call back


	//temp stuff for meeting

	// init_pose.pose.position.x = kf_pos_grid_x_us;
	// init_pose.pose.position.y = kf_pos_grid_z_us;
	// // ROS_INFO("Publishing current pose: (%f, %f)\n", kf_pos_grid_x_us, kf_pos_grid_z_us);
	// init_pose.pose.position.z = 0;
	// // curr_pose.pose.orientation = kf_orientation;
	// init_pose.pose.orientation.x = kf_orientation.x;
	// init_pose.pose.orientation.y = kf_orientation.z;
	// init_pose.pose.orientation.z = kf_orientation.y;
	// init_pose.pose.orientation.w = -kf_orientation.w;
	// cv::Mat(6, 6, CV_64FC1, curr_pose.covariance.elems).setTo(0);
	// init_pose_stamped.header.frame_id = "map";
	// init_pose_stamped.header.stamp = ros::Time::now();
	// init_pose_stamped.header.seq = ++init_pose_id;
	// init_pose_stamped.pose = init_pose;

	// pub_initial_pose.publish(init_pose_stamped);
    //if enable_goal publishing is set, init_pose is start position-from where the robot is supposed to start?
	if (enable_goal_publishing) {
		if (kf_id == 0) { //if it is the first key frame, it is our start position, with key grame id=0
			init_pose.pose.position.x = kf_pos_grid_x_us;
			init_pose.pose.position.y = kf_pos_grid_z_us;
			ROS_INFO("Publishing initial pose: (%f, %f)\n", kf_pos_grid_x_us, kf_pos_grid_z_us);
			init_pose.pose.position.z = 0;
			//init_pose.pose.orientation = kf_orientation;
			init_pose.pose.orientation.x = 0;
			init_pose.pose.orientation.y = 0;
			init_pose.pose.orientation.z = 0;
			init_pose.pose.orientation.w = 1;
			cv::Mat(6, 6, CV_64FC1, init_pose.covariance.elems).setTo(0);
			init_pose_stamped.header.frame_id = "map";
			init_pose_stamped.header.stamp = ros::Time::now();
			init_pose_stamped.header.seq = ++init_pose_id;
			init_pose_stamped.pose = init_pose;
			pub_initial_pose.publish(init_pose_stamped); //the first key frame's initial pose published for AMCL
			// pub_current_pose.publish(init_pose.pose);
			geometry_msgs::PoseArray curr_particles;
			curr_particles.header = init_pose_stamped.header;
			curr_particles.poses.push_back(init_pose.pose);
			pub_current_particles.publish(curr_particles);
		}
		else if (kf_id % goal_gap == 0) {
			if (goal_id>0){ //if it is not the first key frame and we have covered enough key frames, given by gap parameter to publish a goal, we do so
				curr_pose.pose = goal.pose; //the current pose is set to be the goal
				// ROS_INFO("Publishing current pose: (%f, %f)\n",curr_pose.pose.position.x, curr_pose.pose.position.y);
				//curr_pose.pose.position.z = 0;
				////init_pose.pose.orientation = kf_orientation;
				//cv::Mat(6, 6, CV_64FC1, curr_pose.covariance.elems).setTo(0);



				// pub_current_pose.publish(curr_pose.pose);
				geometry_msgs::PoseArray curr_particles;
				curr_particles.header = curr_pose_stamped.header;
				curr_particles.poses.push_back(curr_pose.pose);
				pub_current_particles.publish(curr_particles);
			}

			ROS_INFO("Publishing goal: (%f, %f)\n", kf_pos_grid_x_us, kf_pos_grid_z_us);
			//this publishes the position of the goal key frame-which is used by goal call back function
			// goal.pose.position.x = kf_pos_grid_x_us;
			// goal.pose.position.y = kf_pos_grid_z_us;
			// goal.pose.orientation.x = kf_orientation.x;
			// goal.pose.orientation.y = kf_orientation.z;
			// goal.pose.orientation.z = kf_orientation.y;
			// goal.pose.orientation.w = 1;
			goal.header.frame_id = "map";
			goal.header.stamp = ros::Time::now();
			goal.header.seq = ++goal_id;
			// pub_goal.publish(goal);//why is this commented out??
		}
		//	
		//::ros::console::print();
	}
	nav_msgs::MapMetaData map_metadata;
	map_metadata.width = w;
	map_metadata.height = h;
	map_metadata.resolution = 1.0 / scale_factor;
	map_metadata.map_load_time = grid_map_msg.info.map_load_time;
	map_metadata.origin.position.x = 0;
	map_metadata.origin.position.y = 0;
	map_metadata.origin.position.z = 0;
	pub_grid_map.publish(grid_map_msg);
	pub_grid_map_metadata.publish(map_metadata);
	++kf_id; //advance to the next key frame
		
	//goal.target_pose.header.stamp = ros::Time::now();
	//goal.target_pose.pose.position.x = kf_pos_grid_x;
	//goal.target_pose.pose.position.y = kf_pos_grid_z;
	//goal.target_pose.pose.orientation = pts_and_pose->poses[0].orientation;
	//ROS_INFO("Sending goal");
	//ac.sendGoal(goal);
	//ac.waitForResult();
	//if (ac.getState() == actionlib::SimpleClientGoalState::SUCCEEDED)
	//	ROS_INFO("Hooray, the base moved 1 meter forward");
	//else
	//	ROS_INFO("The base failed to move forward 1 meter for some reason");
}
void loopClosingCallback(const geometry_msgs::PoseArray::ConstPtr& all_kf_and_pts){
	//ROS_INFO("Received points and pose: [%s]{%d}", pts_and_pose->header.frame_id.c_str(),
	//	pts_and_pose->header.seq);
	//if (all_kf_and_pts->header.seq == 0) {
	//	cv::destroyAllWindows();
	//	saveMap();
	//	ros::shutdown();
	//	exit(0);
	//}
	loop_closure_being_processed = true;
	resetGridMap(all_kf_and_pts);
	loop_closure_being_processed = false;
}

void getMixMax(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose,
	geometry_msgs::Point& min_pt, geometry_msgs::Point& max_pt) {

	min_pt.x = min_pt.y = min_pt.z = std::numeric_limits<double>::infinity();
	max_pt.x = max_pt.y = max_pt.z = -std::numeric_limits<double>::infinity();
	for (unsigned int i = 0; i < pts_and_pose->poses.size(); ++i){
		const geometry_msgs::Point& curr_pt = pts_and_pose->poses[i].position;
		if (curr_pt.x < min_pt.x) { min_pt.x = curr_pt.x; }
		if (curr_pt.y < min_pt.y) { min_pt.y = curr_pt.y; }
		if (curr_pt.z < min_pt.z) { min_pt.z = curr_pt.z; }

		if (curr_pt.x > max_pt.x) { max_pt.x = curr_pt.x; }
		if (curr_pt.y > max_pt.y) { max_pt.y = curr_pt.y; }
		if (curr_pt.z > max_pt.z) { max_pt.z = curr_pt.z; }
	}
}
void processMapPt(const geometry_msgs::Point &curr_pt, cv::Mat &occupied, cv::Mat &visited, 
	cv::Mat &pt_mask, int kf_pos_grid_x, int kf_pos_grid_z, unsigned int pt_id) {
	float pt_pos_x = curr_pt.x*scale_factor;
	float pt_pos_z = curr_pt.z*scale_factor;

	int pt_pos_grid_x = int(floor((pt_pos_x - grid_min_x) * norm_factor_x));
	int pt_pos_grid_z = int(floor((pt_pos_z - grid_min_z) * norm_factor_z));

	if (pt_pos_grid_x < 0 || pt_pos_grid_x >= w)
		return;

	if (pt_pos_grid_z < 0 || pt_pos_grid_z >= h)
		return;
	bool is_ground_pt = false;
	bool is_in_horizontal_plane = false;
	if (use_height_thresholding){
		float pt_pos_y = curr_pt.y*scale_factor;
		Eigen::Vector4d transformed_point_location = transform_mat * Eigen::Vector4d(pt_pos_x, pt_pos_y, pt_pos_z, 1);
		double transformed_point_height = transformed_point_location[1] / transformed_point_location[3];
		is_ground_pt = transformed_point_height < 0;
	}
#ifndef DISABLE_FLANN
	if (use_plane_normals) {
		double normal_angle_y_rad = normal_angle_y[pt_id];
		is_in_horizontal_plane = normal_angle_y_rad < normal_thresh_y_rad;
	}
#endif
	if (is_ground_pt || is_in_horizontal_plane) {
		++visited.at<float>(pt_pos_grid_z, pt_pos_grid_x);
	}
	else {
		// Increment the occupency account of the grid cell where map point is located
		++occupied.at<float>(pt_pos_grid_z, pt_pos_grid_x);
		pt_mask.at<uchar>(pt_pos_grid_z, pt_pos_grid_x) = 255;
	}	

	//cout << "----------------------" << endl;
	//cout << okf_pos_grid_x << " " << okf_pos_grid_y << endl;

	// Get all grid cell that the line between keyframe and map point pass through
	int x0 = kf_pos_grid_x;
	int y0 = kf_pos_grid_z;
	int x1 = pt_pos_grid_x;
	int y1 = pt_pos_grid_z;
	bool steep = (abs(y1 - y0) > abs(x1 - x0));
	if (steep){
		swap(x0, y0);
		swap(x1, y1);
	}
	if (x0 > x1){
		swap(x0, x1);
		swap(y0, y1);
	}
	int dx = x1 - x0;
	int dy = abs(y1 - y0);
	double error = 0;
	double deltaerr = ((double)dy) / ((double)dx);
	int y = y0;
	int ystep = (y0 < y1) ? 1 : -1;
	for (int x = x0; x <= x1; ++x){
		if (steep) {
			++visited.at<float>(x, y);
		}
		else {
			++visited.at<float>(y, x);
		}
		error = error + deltaerr;
		if (error >= 0.5){
			y = y + ystep;
			error = error - 1.0;
		}
	}
}


void processGoalandPoints(int kf_pos_grid_x, int kf_pos_grid_z) {
	// ROS_INFO("current DFS pose: (%i, %i)\n", kf_pos_grid_x, kf_pos_grid_z);
	// ROS_INFO("current DFS goal: (%f, %f)\n", goal.pose.position.x, goal.pose.position.y);
	// cv::Size s = grid_map.size();
	// ROS_INFO("current map size: (%i, %i)\n", s.height, s.width);

	// ROS_INFO("current map value: (%f)\n", grid_map.at<float>(kf_pos_grid_x, kf_pos_grid_z));
}	

void processMapPts(const std::vector<geometry_msgs::Pose> &pts, unsigned int n_pts,
	unsigned int start_id, int kf_pos_grid_x, int kf_pos_grid_z) {
	unsigned int end_id = start_id + n_pts;
#ifndef DISABLE_FLANN
	if (use_plane_normals) {
		cv::Mat cv_dataset(n_pts, 3, CV_64FC1);
		for (unsigned int pt_id = start_id; pt_id < end_id; ++pt_id){
			cv_dataset.at<double>(pt_id - start_id, 0) = pts[pt_id].position.x;
			cv_dataset.at<double>(pt_id - start_id, 1) = pts[pt_id].position.y;
			cv_dataset.at<double>(pt_id - start_id, 2) = pts[pt_id].position.z;
		}
		//printf("building FLANN index...\n");		
		flann_index->buildIndex(flannMatT((double *)(cv_dataset.data), n_pts, 3));
		normal_angle_y.resize(n_pts);
		for (unsigned int pt_id = start_id; pt_id < end_id; ++pt_id){
			double pt[3], dists[3];
			pt[0] = pts[pt_id].position.x;
			pt[1] = pts[pt_id].position.y;
			pt[2] = pts[pt_id].position.z;

			int results[3];
			flannMatT flann_query(pt, 1, 3);
			flannMatT flann_dists(dists, 1, 3);
			flannResultT flann_result(results, 3, 1);
			flann_index->knnSearch(flann_query, flann_result, flann_dists, 3, flann::SearchParams());
			Eigen::Matrix3d nearest_pts;
			//printf("Point %d: %f, %f, %f\n", pt_id - start_id, pt[0], pt[1], pt[2]);
			for (unsigned int i = 0; i < 3; ++i){
				nearest_pts(0, i) = cv_dataset.at<double>(results[i], 0);
				nearest_pts(1, i) = cv_dataset.at<double>(results[i], 1);
				nearest_pts(2, i) = cv_dataset.at<double>(results[i], 2);
				//printf("Nearest Point %d: %f, %f, %f\n", results[i], nearest_pts(0, i), nearest_pts(1, i), nearest_pts(2, i));
			}
			Eigen::Vector3d centroid = nearest_pts.rowwise().mean();
			//printf("centroid %f, %f, %f\n", centroid[0], centroid[1], centroid[2]);
			Eigen::Matrix3d centered_pts = nearest_pts.colwise() - centroid;
			Eigen::JacobiSVD<Eigen::Matrix3d> svd(centered_pts, Eigen::ComputeThinU | Eigen::ComputeThinV);
			int n_cols = svd.matrixU().cols();
			// left singular vector corresponding to the smallest singular value
			Eigen::Vector3d normal_direction = svd.matrixU().col(n_cols - 1);
			//printf("normal_direction %f, %f, %f\n", normal_direction[0], normal_direction[1], normal_direction[2]);
			// angle to y axis
			normal_angle_y[pt_id-start_id] = acos(normal_direction[1]);
			if (normal_angle_y[pt_id - start_id ]> (M_PI / 2.0)) {
				normal_angle_y[pt_id - start_id] = M_PI - normal_angle_y[pt_id - start_id];
			}
			//printf("normal angle: %f rad or %f deg\n", normal_angle_y[pt_id - start_id], normal_angle_y[pt_id - start_id]*180.0/M_PI);
			//printf("\n\n");
		}
	}
#endif
	if (use_local_counters) {
		local_map_pt_mask.setTo(0);
		local_occupied_counter.setTo(0);
		local_visit_counter.setTo(0);
		for (unsigned int pt_id = start_id; pt_id < end_id; ++pt_id){
			processMapPt(pts[pt_id].position, local_occupied_counter, local_visit_counter,
				local_map_pt_mask, kf_pos_grid_x, kf_pos_grid_z, pt_id - start_id);
		}
		for (int row = 0; row < h; ++row){
			for (int col = 0; col < w; ++col){
				if (local_map_pt_mask.at<uchar>(row, col) == 0) {
					local_occupied_counter.at<float>(row, col) = 0;
				}
				else {
					local_occupied_counter.at<float>(row, col) = local_visit_counter.at<float>(row, col);
				}
			}
		}
		if (use_gaussian_counters) {
			cv::filter2D(local_occupied_counter, local_occupied_counter, CV_32F, gauss_kernel);
			cv::filter2D(local_visit_counter, local_visit_counter, CV_32F, gauss_kernel);
		}
		global_occupied_counter += local_occupied_counter;
		global_visit_counter += local_visit_counter;
		//cout << "local_occupied_counter: \n" << local_occupied_counter << "\n";
		//cout << "global_occupied_counter: \n" << global_occupied_counter << "\n";
		//cout << "local_visit_counter: \n" << local_visit_counter << "\n";
		//cout << "global_visit_counter: \n" << global_visit_counter << "\n";
	}
	else {
		for (unsigned int pt_id = start_id; pt_id < end_id; ++pt_id){
			processMapPt(pts[pt_id].position, global_occupied_counter, global_visit_counter,
				local_map_pt_mask, kf_pos_grid_x, kf_pos_grid_z, pt_id - start_id);
		}
	}
}

void updateGridMap(const geometry_msgs::PoseArray::ConstPtr& pts_and_pose){

	//geometry_msgs::Point min_pt, max_pt;
	//getMixMax(pts_and_pose, min_pt, max_pt);
	//printf("max_pt: %f, %f\t min_pt: %f, %f\n", max_pt.x*scale_factor, max_pt.z*scale_factor, 
	//	min_pt.x*scale_factor, min_pt.z*scale_factor);

	//double grid_res_x = max_pt.x - min_pt.x, grid_res_z = max_pt.z - min_pt.z;

	//printf("Received frame %u \n", pts_and_pose->header.seq);
    //this function is taking the pts_pose array comming from the publisher
	//then it is using those x,y,z co ordinates and trying to construct the 
	//grid map from it.

	kf_location = pts_and_pose->poses[0].position;
	kf_orientation = pts_and_pose->poses[0].orientation;

	kf_pos_x = kf_location.x*scale_factor;
	kf_pos_z = kf_location.z*scale_factor;

	kf_pos_grid_x = int(floor((kf_pos_x - grid_min_x) * norm_factor_x));
	kf_pos_grid_z = int(floor((kf_pos_z - grid_min_z) * norm_factor_z));

	if (kf_pos_grid_x < 0 || kf_pos_grid_x >= w)
		return;

	if (kf_pos_grid_z < 0 || kf_pos_grid_z >= h)
		return;

	++n_kf_received;

	if (use_height_thresholding){
		Eigen::Vector4d kf_orientation_eig(kf_orientation.w, kf_orientation.x, kf_orientation.y, kf_orientation.z);
		kf_orientation_eig.array() /= kf_orientation_eig.norm();
		Eigen::Matrix3d keyframe_rotation = Eigen::Quaterniond(kf_orientation_eig).toRotationMatrix();
		Eigen::Vector3d keyframe_translation(kf_location.x*scale_factor, kf_location.y*scale_factor, kf_location.z*scale_factor);
		transform_mat.setIdentity();
		transform_mat.topLeftCorner<3, 3>() = keyframe_rotation.transpose();
		transform_mat.topRightCorner<3, 1>() = (-keyframe_rotation.transpose() * keyframe_translation);
	}
	unsigned int n_pts = pts_and_pose->poses.size() - 1;
	//printf("Processing key frame %u and %u points\n",n_kf_received, n_pts);

	// processGoalandPoints(kf_pos_grid_x, kf_pos_grid_z);

	processMapPts(pts_and_pose->poses, n_pts, 1, kf_pos_grid_x, kf_pos_grid_z);


	getGridMap();
	showGridMap(pts_and_pose->header.seq);
	//cout << endl << "Grid map saved!" << endl;
}

void resetGridMap(const geometry_msgs::PoseArray::ConstPtr& all_kf_and_pts){
	global_visit_counter.setTo(0);
	global_occupied_counter.setTo(0);

	unsigned int n_kf = all_kf_and_pts->poses[0].position.x;
	if ((unsigned int) (all_kf_and_pts->poses[0].position.y) != n_kf ||
		(unsigned int) (all_kf_and_pts->poses[0].position.z) != n_kf) {
		printf("resetGridMap :: Unexpected formatting in the keyframe count element\n");
		printf("all_kf_and_pts->poses[0].position.x: %u\n", (unsigned int)(all_kf_and_pts->poses[0].position.x));
		printf("all_kf_and_pts->poses[0].position.y: %u\n", (unsigned int)(all_kf_and_pts->poses[0].position.y));
		printf("all_kf_and_pts->poses[0].position.z: %u\n", (unsigned int)(all_kf_and_pts->poses[0].position.z));
		return;
	}
	printf("Resetting grid map with %d key frames\n", n_kf);
#ifdef COMPILEDWITHC11
	std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
#else
	std::chrono::monotonic_clock::time_point t1 = std::chrono::monotonic_clock::now();
#endif
	unsigned int id = 0;
	for (unsigned int kf_id = 0; kf_id < n_kf; ++kf_id){
		++id;
		kf_location = all_kf_and_pts->poses[id].position;
		kf_orientation = all_kf_and_pts->poses[id].orientation;
		++id;
		unsigned int n_pts = all_kf_and_pts->poses[id].position.x;
		if ((unsigned int)(all_kf_and_pts->poses[id].position.y) != n_pts ||
			(unsigned int)(all_kf_and_pts->poses[id].position.z) != n_pts) {
			printf("resetGridMap :: Unexpected formatting in the point count element for keyframe %d\n", kf_id);
			printf("all_kf_and_pts->poses[%u].position.x: %u\n", id, (unsigned int)(all_kf_and_pts->poses[id].position.x));
			printf("all_kf_and_pts->poses[%u].position.y: %u\n", id, (unsigned int)(all_kf_and_pts->poses[id].position.y));
			printf("all_kf_and_pts->poses[%u].position.z: %u\n", id, (unsigned int)(all_kf_and_pts->poses[id].position.z));
			return;
		}
		float kf_pos_x = kf_location.x*scale_factor;
		float kf_pos_z = kf_location.z*scale_factor;

		int kf_pos_grid_x = int(floor((kf_pos_x - grid_min_x) * norm_factor_x));
		int kf_pos_grid_z = int(floor((kf_pos_z - grid_min_z) * norm_factor_z));

		if (kf_pos_grid_x < 0 || kf_pos_grid_x >= w)
			continue;

		if (kf_pos_grid_z < 0 || kf_pos_grid_z >= h)
			continue;

		if (id + n_pts >= all_kf_and_pts->poses.size()) {
			printf("resetGridMap :: Unexpected end of the input array while processing keyframe %u with %u points: only %u out of %u elements found\n",
				kf_id, n_pts, all_kf_and_pts->poses.size(), id + n_pts);
			return;
		}
		if (use_height_thresholding){
			Eigen::Vector4d kf_orientation_eig(kf_orientation.w, kf_orientation.x, kf_orientation.y, kf_orientation.z);
			kf_orientation_eig.array() /= kf_orientation_eig.norm();
			Eigen::Matrix3d keyframe_rotation = Eigen::Quaterniond(kf_orientation_eig).toRotationMatrix();
			Eigen::Vector3d keyframe_translation(kf_location.x, kf_location.y, kf_location.z);
			transform_mat.setIdentity();
			transform_mat.topLeftCorner<3, 3>() = keyframe_rotation.transpose();
			transform_mat.bottomLeftCorner<1, 3>() = (-keyframe_rotation.transpose() * keyframe_translation).transpose();
		}
		processMapPts(all_kf_and_pts->poses, n_pts, id + 1, kf_pos_grid_x, kf_pos_grid_z);
		id += n_pts;
	}	
	getGridMap();
#ifdef COMPILEDWITHC11
	std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
#else
	std::chrono::monotonic_clock::time_point t2 = std::chrono::monotonic_clock::now();
#endif
	double ttrack = std::chrono::duration_cast<std::chrono::duration<double> >(t2 - t1).count();
	printf("Done. Time taken: %f secs\n", ttrack);
	pub_grid_map.publish(grid_map_msg);
	showGridMap(all_kf_and_pts->header.seq);
}

void getGridMap() {
	double 	unknown_region_ratio = (free_thresh + occupied_thresh) / 2.0;
	for (int row = 0; row < h; ++row){
		for (int col = 0; col < w; ++col){
			float visits = global_visit_counter.at<float>(row, col);
			float occupieds = global_occupied_counter.at<float>(row, col);

			if (visits <= visit_thresh){
				grid_map.at<float>(row, col) = unknown_region_ratio;
			}
			else {
				grid_map.at<float>(row, col) = 1.0 - float(occupieds / visits);
			}
			if (grid_map.at<float>(row, col) >= free_thresh) {
				grid_map_thresh.at<uchar>(row, col) = 255;
				grid_map_int.at<char>(row, col) = (1 - grid_map.at<float>(row, col)) * 100;
			}
			else if (grid_map.at<float>(row, col) < free_thresh && grid_map.at<float>(row, col) >= occupied_thresh) {
				grid_map_thresh.at<uchar>(row, col) = 128;
				grid_map_int.at<char>(row, col) = -1;
			}
			else {
				grid_map_thresh.at<uchar>(row, col) = 0;
				grid_map_int.at<char>(row, col) = (1 - grid_map.at<float>(row, col)) * 100;
			}
		}
	}
	if (use_boundary_detection) {
		cv::Mat canny_output;
		std::vector<std::vector<cv::Point> > contours;
		std::vector<cv::Vec4i> hierarchy;
		cv::Canny(grid_map_thresh, canny_output, canny_thresh, canny_thresh * 2, 3);
		cv::imshow("canny_output", canny_output);
		for (int row = 0; row < h; ++row){
			for (int col = 0; col < w; ++col){
				if (canny_output.at<uchar>(row, col)>0) {
					grid_map_thresh.at<uchar>(row, col) = 0;
					grid_map_int.at<char>(row, col) = 100;
				}
			}
		}
		//cv::findContours(canny_output, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_NONE, cv::Point(0, 0));
		//drawContours(grid_map_thresh, contours, -1, CV_RGB(0, 0, 0), 1, CV_AA);
	}
	cv::resize(grid_map_thresh, grid_map_thresh_resized, grid_map_thresh_resized.size());
}
void showGridMap(unsigned int id) {
	cv::imshow("grid_map_msg", cv::Mat(h, w, CV_8SC1, (char*)(grid_map_msg.data.data())));
	if (show_camera_location) {
		cv::cvtColor(grid_map_thresh_resized, grid_map_rgb, cv::COLOR_GRAY2BGR);
		cv::Scalar line_Color(255, 0, 0);//Color of the circle
		cv::circle(grid_map_rgb, cv::Point(kf_pos_grid_x*resize_factor, kf_pos_grid_z*resize_factor),
			3, line_Color, -1);
		cv::imshow("grid_map_thresh_resized_rgb", grid_map_rgb);
	}
	else {
		cv::imshow("grid_map_thresh_resized", grid_map_thresh_resized);
	}
	//cv::imshow("grid_map", grid_map);
	int key = cv::waitKey(1);
	int key_mod = key % 256;
	bool normal_thresh_updated = false;
	if (key == 27 || key_mod == 27) {
		cv::destroyAllWindows();
		ros::shutdown();
		exit(0);
	}
	else if (key == 'f' || key_mod == 'f') {
		free_thresh -= thresh_diff;
		if (free_thresh <= occupied_thresh){ free_thresh = occupied_thresh + thresh_diff; }

		printf("Setting free_thresh to: %f\n", free_thresh);
	}
	else if (key == 'F' || key_mod == 'F') {
		free_thresh += thresh_diff;
		if (free_thresh > 1){ free_thresh = 1; }
		printf("Setting free_thresh to: %f\n", free_thresh);
	}
	else if (key == 'o' || key_mod == 'o') {
		occupied_thresh -= thresh_diff;
		if (free_thresh < 0){ free_thresh = 0; }
		printf("Setting occupied_thresh to: %f\n", occupied_thresh);
	}
	else if (key == 'O' || key_mod == 'O') {
		occupied_thresh += thresh_diff;
		if (occupied_thresh >= free_thresh){ occupied_thresh = free_thresh - thresh_diff; }
		printf("Setting occupied_thresh to: %f\n", occupied_thresh);
	}
	else if (key == 'b' || key_mod == 'b' || key == 'B' || key_mod == 'B') {
		use_boundary_detection = !use_boundary_detection;
		if (use_boundary_detection){
			printf("Enabling boundary detection\n");
		}
		else {
			cv::destroyWindow("canny_output");
			printf("Disabling boundary detection\n");
		}
	}
	else if (key == 'h' || key_mod == 'h' || key == 'H' || key_mod == 'H') {
		use_height_thresholding = !use_height_thresholding;
		if (use_height_thresholding){
			printf("Enabling height thresholding\n");
		}
		else {
			printf("Disabling height thresholding\n");
		}
	}
	else if (key == 'v' || key_mod == 'v') {
		--visit_thresh;
		printf("Setting normal threshold to: %d\n", visit_thresh);
	}
	else if (key == 'V' || key_mod == 'V') {
		++visit_thresh;
		printf("Setting visit threshold to: %d\n", visit_thresh);
	}
	else if (key == 'c' || key_mod == 'c') {
		canny_thresh -= 10;
		printf("Setting Canny threshold to: %d\n", canny_thresh);
	}
	else if (key == 'C' || key_mod == 'C') {
		canny_thresh += 10;
		printf("Setting Canny threshold to: %d\n", canny_thresh);
	}
	else if (key == 'n' || key_mod == 'n') {
		--normal_thresh_deg;
		printf("Setting normal threshold to: %f degrees\n", normal_thresh_deg);
		normal_thresh_updated = true;
	}
	else if (key == 'F' || key_mod == 'F') {
		++normal_thresh_deg;
		printf("Setting normal threshold to: %f degrees\n", normal_thresh_deg);
		normal_thresh_updated = true;
	}
	else if (key == 's' || key_mod == 's' || key == 'S' || key_mod == 'S') {
		saveMap(id);
	}
	if (normal_thresh_updated){
		if (normal_thresh_deg > 0 && normal_thresh_deg <= 90) {
			use_plane_normals = true;
			normal_thresh_y_rad = (90 - normal_thresh_deg)*M_PI / 180.0;
			printf("normal_thresh_y_rad: %f rad\n", normal_thresh_y_rad);
		}
		else {
			use_plane_normals = false;
		}
	}
}

void parseParams(int argc, char **argv) {
	int arg_id = 1;
	if (argc > arg_id){
		scale_factor = atof(argv[arg_id++]);
	}
	if (argc > arg_id){
		resize_factor = atof(argv[arg_id++]);
	}
	if (argc > arg_id){
		cloud_max_x = atof(argv[arg_id++]);
	}
	if (argc > arg_id){
		cloud_min_x = atof(argv[arg_id++]);
	}	
	if (argc > arg_id){
		cloud_max_z = atof(argv[arg_id++]);
	}
	if (argc > arg_id){
		cloud_min_z = atof(argv[arg_id++]);
	}
	if (argc > arg_id){
		free_thresh = atof(argv[arg_id++]);
	}
	if (argc > arg_id){
		occupied_thresh = atof(argv[arg_id++]);
	}
	if (argc > arg_id){
		use_local_counters = atoi(argv[arg_id++]);
	}
	if (argc > arg_id){
		visit_thresh = atoi(argv[arg_id++]);
	}
	if (argc > arg_id){
		use_gaussian_counters = atoi(argv[arg_id++]);
	}
	if (argc > arg_id){
		use_boundary_detection = atoi(argv[arg_id++]);
	}
	if (argc > arg_id){
		use_height_thresholding = atoi(argv[arg_id++]);
	}
#ifndef DISABLE_FLANN
	if (argc > arg_id){
		normal_thresh_deg = atof(argv[arg_id++]);
	}
#endif
	if (argc > arg_id){
		canny_thresh = atoi(argv[arg_id++]);
	}
	if (argc > arg_id){
		enable_goal_publishing = atoi(argv[arg_id++]);
	}
	if (argc > arg_id){
		show_camera_location = atoi(argv[arg_id++]);
	}
	if (argc > arg_id){
		gaussian_kernel_size = atoi(argv[arg_id++]);
	}
	if (argc > arg_id){
		cam_radius = atoi(argv[arg_id++]);
	}
}

void printParams() {
	printf("Using params:\n");
	printf("scale_factor: %f\n", scale_factor);
	printf("resize_factor: %f\n", resize_factor);
	printf("cloud_max: %f, %f\t cloud_min: %f, %f\n", cloud_max_x, cloud_max_z, cloud_min_x, cloud_min_z);
	//printf("cloud_min: %f, %f\n", cloud_min_x, cloud_min_z);
	printf("free_thresh: %f\n", free_thresh);
	printf("occupied_thresh: %f\n", occupied_thresh);
	printf("use_local_counters: %d\n", use_local_counters);
	printf("visit_thresh: %d\n", visit_thresh);
	printf("use_gaussian_counters: %d\n", use_gaussian_counters);
	printf("use_boundary_detection: %d\n", use_boundary_detection);
	printf("use_height_thresholding: %d\n", use_height_thresholding);
#ifndef DISABLE_FLANN
	printf("normal_thresh_deg: %f\n", normal_thresh_deg);
#endif
	printf("canny_thresh: %d\n", canny_thresh);
	printf("enable_goal_publishing: %d\n", enable_goal_publishing);
	printf("show_camera_location: %d\n", show_camera_location);
	printf("gaussian_kernel_size: %d\n", gaussian_kernel_size);
	printf("cam_radius: %d\n", cam_radius);
}

