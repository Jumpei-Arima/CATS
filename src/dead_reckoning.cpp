/*/imu/dataと/odomから軌跡をpubする
 * 
 * author : R.Kusakari
 *
 */
#include <ros/ros.h>
#include <stdio.h>
#include <iostream>
#include <std_msgs/Bool.h>
#include <sensor_msgs/Imu.h>
#include <nav_msgs/Odometry.h>
#include <tf/transform_datatypes.h>
#include <tf/transform_broadcaster.h>
#include <geometry_msgs/Pose2D.h>
#include <geometry_msgs/Pose.h>

using namespace std;	
// #define PITCH_BIAS -0.00145
const double  Z_BIAS = -9.78153;
const double  X_BIAS = -0.47;
#define PITCH_BIAS -0.003120

struct Pre
{
	double x;
	double z;
	double pitch;
	ros::Time time;
};

class Complement{
	private:
		ros::NodeHandle n;
		ros::Rate r;
		ros::Subscriber odm_sub;
		ros::Subscriber imu_sub;
		ros::Publisher lcl_pub;
		ros::Publisher lcl_vis_pub;
		ros::Publisher saka_pub;

		tf::TransformBroadcaster br;
		tf::Transform transform;

		geometry_msgs::Pose2D init_pose;
		std_msgs::Bool saka_flag;
		nav_msgs::Odometry lcl_;
		nav_msgs::Odometry lcl_vis;

		bool start_flag;

int count;
double sum;

		geometry_msgs::Quaternion odom_quat;
		ros::Time current_time;
		ros::Time last_time;
		double odom_vel;
		double dx;
		double dz;
		double dpitch;
		double dyaw;
		double x;
		double y;
		double z;
		double yaw;
		double pitch;
		//param
		string HEADER_FRAME;
		string CHILD_FRAME;
		string ODOM_TOPIC;
		string IMU_TOPIC;
		double drift_dyaw;

		Pre pre_state;
	public:
		Complement(ros::NodeHandle n,ros::NodeHandle priv_nh);
		void odomCallback(const nav_msgs::Odometry::Ptr msg);
		void imuCallback(const sensor_msgs::Imu::Ptr msg);
		void prepare();
		void start();
		void calc();
		bool spin()
		{				
			ros::Rate loop_rate(r);
			
			while(ros::ok()){

				if(start_flag) start();

				ros::spinOnce();
				loop_rate.sleep();
			}
			return true;
		}

};




Complement::Complement(ros::NodeHandle n,ros::NodeHandle priv_nh)
	: r(50), start_flag(false),pitch(0.0)
{

	priv_nh.param("header_frame" ,HEADER_FRAME, {0});
	priv_nh.param("child_frame" ,CHILD_FRAME, {0});
	priv_nh.param("init_x" ,init_pose.x, {0.0});
	priv_nh.param("init_y" ,init_pose.y, {0.0});
	priv_nh.param("init_yaw" ,init_pose.theta, {0.0});	//rad
	priv_nh.param("odom_topic" ,ODOM_TOPIC, {0});
	priv_nh.param("imu_topic" ,IMU_TOPIC, {0});
	priv_nh.param("dyaw/drift",drift_dyaw,{0});

	odm_sub = n.subscribe(ODOM_TOPIC, 100, &Complement::odomCallback, this);
	imu_sub = n.subscribe(IMU_TOPIC, 100, &Complement::imuCallback, this);

	lcl_pub = n.advertise<nav_msgs::Odometry>("lcl_ekf", 10);
	lcl_vis_pub = n.advertise<nav_msgs::Odometry>("/lcl_vis", 10);
	
	saka_pub = n.advertise<std_msgs::Bool>("/saka_flag", 10);

	lcl_.header.frame_id = HEADER_FRAME;
	lcl_.child_frame_id = CHILD_FRAME;
	lcl_vis.header.frame_id = HEADER_FRAME;
	lcl_vis.child_frame_id = CHILD_FRAME;

	x = init_pose.x;
	y = init_pose.y;
	yaw = init_pose.theta;
	prepare();

	z = 0;
	count = 0;
	sum = 0;
}



void 
Complement::odomCallback(const nav_msgs::Odometry::Ptr msg){

	odom_vel = msg->twist.twist.linear.x;
	
}


void 
Complement::imuCallback(const sensor_msgs::Imu::Ptr imu){

	dx = imu->linear_acceleration.x;
	dz = imu->linear_acceleration.z;
	dz += Z_BIAS;
	dpitch = imu->angular_velocity.y + PITCH_BIAS;
	dyaw = imu->angular_velocity.z;
	if("/imu/data/calibrated" != IMU_TOPIC) dyaw -= drift_dyaw;	//tkhsh_imuの方で調節
	current_time = imu->header.stamp;
	if(!start_flag) {
		last_time = current_time;
		pre_state.time = current_time;
	}

	calc();
}


void
Complement::prepare(){

	transform.setOrigin( tf::Vector3(init_pose.x, init_pose.y, 0.0) );
	tf::Quaternion q;
	q.setRPY(0, 0, init_pose.theta);

	transform.setRotation(q);
	br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), HEADER_FRAME, CHILD_FRAME));

}


void
Complement::calc(){
	
	double last_accurate,current_accurate;
	last_accurate = (double)last_time.nsec*1.0e-9 + last_time.sec;
	current_accurate = (double)current_time.nsec*1.0e-9 + current_time.sec;
	double dt = current_accurate - last_accurate;
	last_time = current_time;

	double dist = odom_vel * dt; 

	z += dz * dt * (-1);		//重力加速度だからマイナス
	pitch += dpitch * dt;
	yaw += dyaw * dt;
	while(yaw > M_PI) yaw -= 2*M_PI;
	while(yaw < -M_PI) yaw += 2*M_PI;

	if(!start_flag){
		pre_state.z = z;
		pre_state.pitch = pitch;
	}
	start_flag = true;

	if((current_time - pre_state.time)>ros::Duration(2.0)){
		if((count/sum)>0.8) saka_flag.data = true;
		else saka_flag.data = false;
		pre_state.time = current_time;
		count = sum = 0;
	}
	else{
		if((fabs(dx)-fabs(X_BIAS)) > 0.4) count++;
		sum++;
	}

	x += dist * cos(yaw);// * cos(pitch);
	y += dist * sin(yaw);// * cos(pitch);
	odom_quat = tf::createQuaternionMsgFromYaw(yaw);
	
}




void
Complement::start(){

	lcl_.header.stamp = ros::Time::now();
	lcl_.pose.pose.position.x = x;
	lcl_.pose.pose.position.y = y;
	lcl_.pose.pose.position.z = 0.0;
	lcl_.pose.pose.orientation.z = yaw;

	lcl_pub.publish(lcl_);


	lcl_vis.header.stamp = ros::Time::now();
	lcl_vis.pose.pose.position.x = x;
	lcl_vis.pose.pose.position.y = y;
	lcl_vis.pose.pose.position.z = 0.0;	
	lcl_vis.pose.pose.orientation = odom_quat;

	lcl_vis_pub.publish(lcl_vis);

	transform.setOrigin( tf::Vector3( x, y, 0.0) );
	tf::Quaternion q;
	q.setRPY(0, 0, yaw);

	transform.setRotation(q);
	br.sendTransform(tf::StampedTransform(transform, lcl_.header.stamp , HEADER_FRAME, CHILD_FRAME));

	saka_pub.publish(saka_flag);
}


int main (int argc, char** argv){
	ros::init(argc,argv,"dead_reckoning");
	ros::NodeHandle n;
	ros::NodeHandle priv_nh("~");

	cout<<"------complement start---------"<<endl;
	Complement complement(n,priv_nh);
	complement.spin();

	return 0;
}
