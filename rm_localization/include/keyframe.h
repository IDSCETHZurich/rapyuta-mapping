#ifndef KEYFRAME_H_
#define KEYFRAME_H_

#include <frame.h>
#include <boost/shared_ptr.hpp>
#include <opencv2/core/core.hpp>
#include <sophus/se3.hpp>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/point_types.h>

struct convert_depth_to_pointcloud {
	const uint8_t * intencity;
	const uint16_t * depth;
	const Eigen::Vector3f & intrinsics;
	int cols;
	int rows;
	pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud;
	int16_t * intencity_dx;
	int16_t * intencity_dy;

	convert_depth_to_pointcloud(const uint8_t * intencity, const uint16_t * depth,
			const Eigen::Vector3f & intrinsics, int cols, int rows,
			pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud,
			int16_t * intencity_dx, int16_t * intencity_dy) :
			intencity(intencity), depth(depth), intrinsics(intrinsics), cols(
					cols), rows(rows), cloud(cloud), intencity_dx(intencity_dx), intencity_dy(
					intencity_dy) {
	}

	void operator()(const tbb::blocked_range<int>& range) const {
		pcl::PointXYZRGB bad_point;
		bad_point.x = bad_point.y = bad_point.z =
				std::numeric_limits<float>::quiet_NaN();
		for (int i = range.begin(); i != range.end(); i++) {
			int u = i % cols;
			int v = i / cols;

			int16_t dx, dy;
			if (u == 0) {
				dx = (int16_t) intencity[i + 1] - intencity[i];
			} else if (u == cols - 1) {
				dx = (int16_t) intencity[i] - intencity[i - 1];
			} else {
				dx = ((int16_t) intencity[i + 1] - intencity[i - 1]) / 2;
			}

			if (v == 0) {
				dy = (int16_t) intencity[i + cols] - intencity[i];
			} else if (v == rows - 1) {
				dy = (int16_t) intencity[i] - intencity[i - cols];
			} else {
				dy = ((int16_t) intencity[i + cols] - intencity[i - cols]) / 2;
			}

			intencity_dx[i] = dx;
			intencity_dy[i] = dy;

			pcl::PointXYZRGB p;
			p.z = depth[i] / 1000.0;

			if (p.z > 0 /* && (std::abs(dx) > 12 || std::abs(dy) > 12) */) {
				p.x = (u - intrinsics[1]) * p.z / intrinsics[0];
				p.y = (v - intrinsics[2]) * p.z / intrinsics[0];

				p.r = p.g = p.b = intencity[i];

				cloud->at(u, v) = p;
			} else {
				cloud->at(u, v) = bad_point;
			}

		}

	}
};

struct reduce_jacobian {

	Eigen::MatrixXf JtJ;
	Eigen::VectorXf Jte;

	const uint8_t * intencity;
	const int16_t * intencity_dx;
	const int16_t * intencity_dy;
	const uint8_t * intencity_warped;
	const uint16_t * depth_warped;
	const Eigen::Vector3f & intrinsics;
	const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud;
	int cols;
	int rows;

	reduce_jacobian(const uint8_t * intencity, const int16_t * intencity_dx,
			const int16_t * intencity_dy, const uint8_t * intencity_warped,
			const uint16_t * depth_warped, const Eigen::Vector3f & intrinsics,
			const pcl::PointCloud<pcl::PointXYZRGB>::Ptr & cloud, int cols, int rows) :
			intencity(intencity), intencity_dx(intencity_dx), intencity_dy(
					intencity_dy), intencity_warped(intencity_warped), depth_warped(
					depth_warped), intrinsics(intrinsics), cloud(cloud), cols(cols), rows(rows) {

		JtJ.setZero(6,6);
		Jte.setZero(6);

	}

	reduce_jacobian(reduce_jacobian & rb, tbb::split) :
			intencity(rb.intencity), intencity_dx(rb.intencity_dx), intencity_dy(
					rb.intencity_dy), intencity_warped(rb.intencity_warped), depth_warped(
					rb.depth_warped), intrinsics(rb.intrinsics), cloud(rb.cloud), cols(rb.cols), rows(rb.rows) {
		JtJ.setZero(6,6);
		Jte.setZero(6);
	}

	inline void compute_jacobian(const pcl::PointXYZRGB & p,
			Eigen::MatrixXf & j) {

		float z = 1.0f / p.z;
		float z_sqr = 1.0f / (p.z * p.z);

		j(0, 0) = z;
		j(0, 1) = 0.0f;
		j(0, 2) = -p.x * z_sqr;
		j(0, 3) = j(0, 2) * p.y; //j(0, 3) = -p(0) * p(1) * z_sqr;
		j(0, 4) = 1.0f - j(0, 2) * p.x; //j(0, 4) =  (1.0 + p(0) * p(0) * z_sqr);
		j(0, 5) = -p.y * z;

		j(1, 0) = 0.0f;
		j(1, 1) = z;
		j(1, 2) = -p.y * z_sqr;
		j(1, 3) = -1.0f + j(1, 2) * p.y; //j(1, 3) = -(1.0 + p(1) * p(1) * z_sqr);
		j(1, 4) = -j(0, 3); //j(1, 4) =  p(0) * p(1) * z_sqr;
		j(1, 5) = p.x * z;

	}

	void operator()(const tbb::blocked_range<int>& range) {
		for (int i = range.begin(); i != range.end(); i++) {
			int u = i % cols;
			int v = i / cols;

			pcl::PointXYZRGB p = cloud->at(u, v);
			if (std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
					&& depth_warped[i] != 0) {

				float error = intencity[i]
						- intencity_warped[i];

				Eigen::VectorXf Ji(2);
				Eigen::MatrixXf Jw(2,6);
				Eigen::VectorXf J(6);
				Ji[0] = intencity_dx[i] * intrinsics[0];
				Ji[1] = intencity_dy[i] * intrinsics[0];

				compute_jacobian(p, Jw);

				J = Ji.transpose() * Jw;

				JtJ += J * J.transpose();
				Jte += J * error;

			}

		}

	}

	void join(reduce_jacobian& rb) {
		JtJ += rb.JtJ;
		Jte += rb.Jte;
	}

};

class keyframe: public frame {

public:

	typedef boost::shared_ptr<keyframe> Ptr;

	keyframe(const cv::Mat & yuv, const cv::Mat & depth,
			const Sophus::SE3f & position, const Eigen::Vector3f & intrinsics,
			int max_level = 3);

	~keyframe();

	void estimate_position(frame & f);

	inline cv::Mat get_i_dx(int level) {
		return cv::Mat(rows / (1 << level), cols / (1 << level), CV_16S,
				intencity_pyr_dx[level]);
	}

	inline cv::Mat get_i_dy(int level) {
		return cv::Mat(rows / (1 << level), cols / (1 << level), CV_16S,
				intencity_pyr_dy[level]);
	}

	inline Eigen::Vector3f get_intrinsics(int level) {
		return intrinsics / (1 << level);
	}

	inline pcl::PointCloud<pcl::PointXYZRGB>::Ptr get_pointcloud(int level) {
		return clouds[level];
	}

protected:
	int16_t ** intencity_pyr_dx;
	int16_t ** intencity_pyr_dy;
	Eigen::Vector3f intrinsics;

	std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> clouds;

};

#endif /* KEYFRAME_H_ */