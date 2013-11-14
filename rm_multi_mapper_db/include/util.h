#include <memory>
#include <iostream>
#include <stdio.h>
#include <malloc.h>
#include <opencv2/opencv.hpp>
#include <opencv2/nonfree/features2d.hpp>

/*MySQL includes */
#include "mysql_connection.h"
#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/prepared_statement.h>

#include <keyframe_map.h>
#include <reduce_measurement_g2o_dist.h>

class DataBuf: public streambuf {
public:
	DataBuf(char * d, size_t s) {
		setg(d, d, d + s);
	}
};

class util {
public:

	util();
	~util();

	struct measurement {
		long first;
		long second;
		Sophus::SE3f transform;
		std::string type;
	};

	struct position {
		long idx;
		Sophus::SE3f transform;
	};


	int get_new_robot_id();
	void add_keyframe(int robot_id, const color_keyframe::Ptr & k);
	void add_measurement(long first, long second,
			const Sophus::SE3f & transform, const std::string & type);

	void add_keypoints(const color_keyframe::Ptr & k);
	void get_keypoints(long frame_id,
			pcl::PointCloud<pcl::PointXYZ> & keypoints3d,
			cv::Mat & desctriptors);

	color_keyframe::Ptr get_keyframe(long frame_id);

	boost::shared_ptr<keyframe_map> get_robot_map(int robot_id);

	void get_overlapping_pairs(int map_id,
			std::vector<std::pair<long, long> > & overlapping_keyframes);

	void load_measurements(long keyframe_id, std::vector<measurement> & m);
	void load_positions(int map_id, std::vector<position> & p);
	void update_position(const position & p);

	void compute_features(const cv::Mat & rgb, const cv::Mat & depth,
			const Eigen::Vector3f & intrinsics,
			std::vector<cv::KeyPoint> & filtered_keypoints,
			pcl::PointCloud<pcl::PointXYZ> & keypoints3d,
			cv::Mat & descriptors);

private:



	std::string server;
	std::string user;
	std::string password;
	std::string database;

	sql::Driver *driver;
	sql::Connection *con;

	cv::Ptr<cv::FeatureDetector> fd;
	cv::Ptr<cv::DescriptorExtractor> de;

	sql::ResultSet* sql_query(std::string query);
	Sophus::SE3f get_pose(sql::ResultSet * res);
	color_keyframe::Ptr get_keyframe(sql::ResultSet * res);
};
