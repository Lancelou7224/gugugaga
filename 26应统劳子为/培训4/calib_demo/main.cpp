#include <opencv2/opencv.hpp>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "用法: calib_demo <图像目录> [--show]\n";
        return 1;
    }
    const fs::path imageDir = argv[1];
    const bool show = argc > 2 && std::string(argv[2]) == "--show";
    const cv::Size boardSize(7, 10); // 棋盘内角点：列×行
    constexpr float squareSize = 1.0f; // 未知实际方格边长，采用相对单位
    const int flags = cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE;
    const cv::TermCriteria criteria(cv::TermCriteria::EPS | cv::TermCriteria::MAX_ITER, 30, 1e-3);

    if (!fs::is_directory(imageDir)) {
        std::cerr << "图像目录不存在: " << imageDir << '\n';
        return 1;
    }
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(imageDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) { std::cerr << "目录中没有支持的图像文件。\n"; return 1; }

    std::vector<cv::Point3f> board;
    for (int y = 0; y < boardSize.height; ++y)
        for (int x = 0; x < boardSize.width; ++x)
            board.emplace_back(x * squareSize, y * squareSize, 0.0f);

    std::vector<std::vector<cv::Point3f>> objectPoints;
    std::vector<std::vector<cv::Point2f>> imagePoints;
    std::vector<cv::Mat> images;
    std::vector<std::string> names;
    cv::Size imageSize;
    fs::create_directories("results");
    fs::create_directories("results/corners");
    fs::create_directories("results/corrected");

    for (const auto& path : files) {
        cv::Mat image = cv::imread(path.string());
        if (image.empty()) { std::cerr << "读取失败，跳过: " << path.filename() << '\n'; continue; }
        if (imageSize.empty()) imageSize = image.size();
        if (image.size() != imageSize) { std::cerr << "尺寸不同，跳过: " << path.filename() << '\n'; continue; }
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        std::vector<cv::Point2f> corners;
        const bool found = cv::findChessboardCorners(gray, boardSize, corners, flags);
        if (found) {
            cv::cornerSubPix(gray, corners, cv::Size(11, 11), cv::Size(-1, -1), criteria);
            objectPoints.push_back(board);
            imagePoints.push_back(corners);
            cv::Mat marked = image.clone();
            cv::drawChessboardCorners(marked, boardSize, corners, true);
            for (int row = 0; row < boardSize.height; ++row) {
                const cv::Scalar color = cv::applyColorMap(cv::Mat(1, 1, CV_8UC1, cv::Scalar(row * 255 / (boardSize.height - 1))), cv::COLORMAP_JET).at<cv::Vec3b>(0,0);
                std::vector<cv::Point> line;
                for (int col = 0; col < boardSize.width; ++col) line.emplace_back(cvRound(corners[row * boardSize.width + col].x), cvRound(corners[row * boardSize.width + col].y));
                cv::polylines(marked, line, false, cv::Scalar(color[0], color[1], color[2]), 2, cv::LINE_AA);
            }
            cv::imwrite((fs::path("results/corners") / (path.stem().string() + ".jpg")).string(), marked);
            names.push_back(path.filename().string());
            std::cout << "角点检出: " << path.filename() << '\n';
        }
        images.push_back(image);
    }
    if (objectPoints.size() < 3) { std::cerr << "有效标定图像少于 3 张，无法标定。\n"; return 2; }

    cv::Mat camera = cv::initCameraMatrix2D(objectPoints, imagePoints, imageSize);
    cv::Mat dist = cv::Mat::zeros(8, 1, CV_64F);
    std::vector<cv::Mat> rvecs, tvecs;
    const double rms = cv::calibrateCamera(objectPoints, imagePoints, imageSize, camera, dist, rvecs, tvecs);
    const cv::Mat newCamera = cv::getOptimalNewCameraMatrix(camera, dist, imageSize, 0.0, imageSize);

    std::ofstream out("results/calibration.txt");
    out << std::fixed << std::setprecision(6)
        << "输入图像: " << files.size() << "\n角点检出并参与标定: " << objectPoints.size() << "\n"
        << "图像尺寸: " << imageSize.width << "x" << imageSize.height << "\n棋盘内角点: 7x10\n"
        << "RMS重投影误差(px): " << rms << "\n相机内参矩阵 K:\n" << camera
        << "\n畸变系数 D [k1,k2,p1,p2,k3,...]:\n" << dist << "\n";
    out.close();
    std::cout << "\n========== 标定结果 ==========\nRMS = " << rms << " px\nK =\n" << camera << "\nD =\n" << dist << '\n';

    for (const auto& path : files) {
        cv::Mat image = cv::imread(path.string());
        if (image.empty() || image.size() != imageSize) continue;
        cv::Mat corrected;
        cv::undistort(image, corrected, camera, dist, newCamera);
        cv::imwrite((fs::path("results/corrected") / (path.stem().string() + ".jpg")).string(), corrected);
        if (show) { cv::imshow("Undistorted", corrected); cv::waitKey(1); }
    }
    const auto sample = cv::imread((imageDir / names[names.size()/2]).string());
    cv::Mat fixed;
    cv::undistort(sample, fixed, camera, dist, newCamera);
    cv::imwrite("results/undistort_before.jpg", sample);
    cv::imwrite("results/undistort_after.jpg", fixed);
    cv::Mat comparison;
    cv::hconcat(sample, fixed, comparison);
    cv::imwrite("results/undistort_compare.jpg", comparison);
    if (show) { cv::imshow("Before | After", comparison); cv::waitKey(0); }
    std::cout << "结果写入当前目录 results/，处理图像 " << files.size() << " 张。\n";
    return 0;
}
