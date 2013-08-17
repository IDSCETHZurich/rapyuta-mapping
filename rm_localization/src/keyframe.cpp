#include <keyframe.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

keyframe::keyframe(const cv::Mat & yuv, const cv::Mat & depth,
		const Sophus::SE3f & position, const Eigen::Vector3f & intrinsics,
		int max_level) :
		frame(yuv, depth, position, max_level) {

	this->intrinsics = intrinsics;

	intencity_pyr_dx = cv::Mat(intencity_pyr.size(), intencity_pyr.type());
	intencity_pyr_dy = cv::Mat(intencity_pyr.size(), intencity_pyr.type());

	for (int level = 0; level < max_level; level++) {
		cv::Sobel(get_i(level), get_i_dx(level), CV_32F, 1, 0);
		cv::Sobel(get_i(level), get_i_dy(level), CV_32F, 0, 1);
	}

	for (int level = 0; level < max_level; level++) {
		cv::Mat d = get_d(level);
		Eigen::Vector3f intrinsics = get_intrinsics(level);
		pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(
				new pcl::PointCloud<pcl::PointXYZ>(d.cols, d.rows));

		convert_depth_to_pointcloud sub(d, intrinsics, cloud);
		tbb::parallel_for(tbb::blocked_range<int>(0, d.cols * d.rows), sub);

		clouds.push_back(cloud);
	}

}

void keyframe::estimate_position(frame & f) {

	int level_iterations[] = { 1, 5, 10 };

	for (int level = 2; level >= 0; level--) {
		for (int iteration = 0; iteration < level_iterations[level];
				iteration++) {

			Sophus::SE3f Mrc = position.inverse() * f.position;

			cv::Mat intencity = get_i(level), depth = get_d(level),
					intencity_dx = get_i_dx(level), intencity_dy = get_i_dy(
							level);

			Eigen::Vector3f intrinsics = get_intrinsics(level);
			cv::Mat intencity_warped(intencity.rows, intencity.cols,
					intencity.type()), depth_warped(depth.rows, depth.cols,
					depth.type());

			f.warp(clouds[level], intrinsics, position, level, intencity_warped,
					depth_warped);

			reduce_jacobian rj(intencity, intencity_dx, intencity_dy,
					intencity_warped, depth_warped, intrinsics, clouds[level]);


			tbb::parallel_reduce(
					tbb::blocked_range<int>(0, intencity.cols * intencity.rows),
					rj);


			//rj(tbb::blocked_range<int>(0, intencity.cols * intencity.rows));

			Sophus::Vector6f update = rj.JtJ.ldlt().solve(rj.Jte);

			std::cerr << "Max update " << std::endl <<  Sophus::SE3f::exp(update).matrix() << std::endl;

			f.position = position * Sophus::SE3f::exp(update) * Mrc;

			//std::cerr << "Transform " << std::endl << f.position.matrix()
			//		<< std::endl;


			if (level == 0) {
				cv::imshow("intencity_warped", intencity_warped);
				cv::imshow("depth_warped", depth_warped);
				cv::imshow("intensity", intencity);
				cv::imshow("depth", depth);
				cv::imshow("residuals", intencity - intencity_warped);
				cv::waitKey(3);
			}

		}
	}

}
