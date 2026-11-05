#include <opencv2/opencv.hpp>

#if __has_include(<opencv2/geometry/2d.hpp>)
#include <opencv2/geometry/2d.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;

constexpr double kPi = 3.14159265358979323846;
constexpr int kSaturationMin = 100;
constexpr int kValueMin = 70;

struct Lightbar
{
  cv::RotatedRect box;
  cv::Point2f center;
  cv::Point2f axis;
  double length;
  double width;
  double angle;
};

struct ArmorPair
{
  Lightbar left;
  Lightbar right;
  double score;
};

cv::Mat redMask(const cv::Mat & frame)
{
  cv::Mat hsv;
  cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

  cv::Mat lowHue;
  cv::Mat highHue;
  cv::Mat mask;
  cv::inRange(hsv, cv::Scalar(0, kSaturationMin, kValueMin), cv::Scalar(10, 255, 255), lowHue);
  cv::inRange(hsv, cv::Scalar(170, kSaturationMin, kValueMin), cv::Scalar(180, 255, 255), highHue);
  cv::bitwise_or(lowHue, highHue, mask);
  cv::morphologyEx(mask, mask,
                   cv::MORPH_CLOSE,
                   cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
  return mask;
}

bool makeLightbar(const cv::RotatedRect & rect, Lightbar & bar)
{
  const double longSide = std::max(rect.size.width, rect.size.height);
  const double shortSide = std::min(rect.size.width, rect.size.height);
  if (shortSide < 1.0 || longSide < 10.0 || longSide / shortSide < 1.5 ||
      longSide / shortSide > 20.0) {
    return false;
  }

  cv::Point2f corners[4];
  rect.points(corners);
  double longestEdge = 0.0;
  cv::Point2f axis;
  for (int i = 0; i < 4; ++i) {
    const cv::Point2f edge = corners[(i + 1) % 4] - corners[i];
    const double edgeLength = cv::norm(edge);
    if (edgeLength > longestEdge) {
      longestEdge = edgeLength;
      axis = edge / static_cast<float>(edgeLength);
    }
  }
  if (axis.y < 0.0F) {
    axis = -axis;
  }

  const double angle = std::atan2(axis.y, axis.x) * 180.0 / kPi;
  const double verticalError = std::abs(90.0 - std::abs(angle));
  if (verticalError > 45.0) {
    return false;
  }

  bar = {rect, rect.center, axis, longSide, shortSide, angle};
  return true;
}

std::vector<Lightbar> findLightbars(const cv::Mat & frame, cv::Mat & mask)
{
  mask = redMask(frame);
  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(mask.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

  std::vector<Lightbar> bars;
  for (const auto & contour : contours) {
    if (cv::contourArea(contour) < 25.0) {
      continue;
    }
    Lightbar bar;
    if (makeLightbar(cv::minAreaRect(contour), bar)) {
      bars.push_back(bar);
    }
  }
  std::sort(bars.begin(), bars.end(), [](const Lightbar & a, const Lightbar & b) {
    return a.center.x < b.center.x;
  });
  return bars;
}

double angleDifference(double a, double b)
{
  double difference = std::fmod(std::abs(a - b), 180.0);
  if (difference > 90.0) {
    difference = 180.0 - difference;
  }
  return difference;
}

bool pairBars(const Lightbar & a, const Lightbar & b, ArmorPair & pair)
{
  const cv::Point2f delta = b.center - a.center;
  const double distance = cv::norm(delta);
  const double meanLength = (a.length + b.length) * 0.5;
  const double spacing = distance / meanLength;
  const double sideRatio = std::max(a.length, b.length) / std::min(a.length, b.length);
  const double barAngleDifference = angleDifference(a.angle, b.angle);

  cv::Point2f meanAxis = a.axis + b.axis;
  if (cv::norm(meanAxis) < 1.0e-5) {
    return false;
  }
  meanAxis /= static_cast<float>(cv::norm(meanAxis));
  const cv::Point2f separation = delta / static_cast<float>(distance);
  const double projection = static_cast<double>(std::abs(meanAxis.dot(separation)));
  const double acrossError = std::asin(std::min(1.0, projection)) * 180.0 / kPi;

  if (spacing < 1.0 || spacing > 5.0 || sideRatio > 1.7 ||
      barAngleDifference > 25.0 || acrossError > 25.0) {
    return false;
  }

  const double score = std::abs(spacing - 2.0) + barAngleDifference / 25.0 +
                       acrossError / 25.0 + sideRatio - 1.0;
  pair = {a, b, score};
  return true;
}

std::vector<cv::Point2f> outsideCorners(const Lightbar & bar, const cv::Point2f & outward)
{
  cv::Point2f points[4];
  bar.box.points(points);
  std::vector<cv::Point2f> sorted(points, points + 4);
  std::sort(sorted.begin(), sorted.end(), [&bar](const cv::Point2f & a, const cv::Point2f & b) {
    return a.dot(bar.axis) < b.dot(bar.axis);
  });

  auto outermost = [&outward](const cv::Point2f & a, const cv::Point2f & b) {
    return a.dot(outward) < b.dot(outward) ? a : b;
  };
  return {outermost(sorted[0], sorted[1]), outermost(sorted[2], sorted[3])};
}

void drawLightbar(cv::Mat & frame, const Lightbar & bar)
{
  const cv::Rect bounds(0, 0, frame.cols, frame.rows);
  const cv::Rect box = bar.box.boundingRect() & bounds;
  if (box.area() > 0) {
    cv::rectangle(frame, box, cv::Scalar(0, 220, 255), 2, cv::LINE_AA);
  }
}

void drawArmor(cv::Mat & frame, const ArmorPair & pair)
{
  cv::Point2f separation = pair.right.center - pair.left.center;
  separation /= static_cast<float>(cv::norm(separation));
  const auto left = outsideCorners(pair.left, -separation);
  const auto right = outsideCorners(pair.right, separation);
  const std::vector<cv::Point> corners = {
    left[0], right[0], right[1], left[1]
  };
  cv::polylines(frame, corners, true, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);

  const char * labels[] = {"P1", "P2", "P3", "P4"};
  for (std::size_t i = 0; i < corners.size(); ++i) {
    cv::circle(frame, corners[i], 5, cv::Scalar(0, 255, 0), cv::FILLED, cv::LINE_AA);
    cv::putText(frame, labels[i], corners[i] + cv::Point(8, -8),
                cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
  }
}

bool bestPair(const std::vector<Lightbar> & bars, ArmorPair & best)
{
  bool found = false;
  for (std::size_t i = 0; i < bars.size(); ++i) {
    for (std::size_t j = i + 1; j < bars.size(); ++j) {
      ArmorPair candidate;
      if (pairBars(bars[i], bars[j], candidate) && (!found || candidate.score < best.score)) {
        best = candidate;
        found = true;
      }
    }
  }
  return found;
}
}  // namespace

int main(int argc, char ** argv)
{
  std::string videoPath = "assets/test_clip.mp4";
  std::string outputPath = "outputs/detection.mp4";
  bool show = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--video" && i + 1 < argc) {
      videoPath = argv[++i];
    } else if (arg == "--out" && i + 1 < argc) {
      outputPath = argv[++i];
    } else if (arg == "--show") {
      show = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: detect_demo [--video path] [--out path] [--show]\n";
      return 0;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      return 2;
    }
  }

  cv::VideoCapture capture(videoPath);
  if (!capture.isOpened()) {
    std::cerr << "Could not open video: " << videoPath << "\n";
    return 1;
  }

  const int width = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH));
  const int height = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT));
  const double fps = capture.get(cv::CAP_PROP_FPS) > 0.0 ? capture.get(cv::CAP_PROP_FPS) : 30.0;
  const fs::path outputParent = fs::path(outputPath).parent_path();
  if (!outputParent.empty()) {
    fs::create_directories(outputParent);
  }
  cv::VideoWriter writer(outputPath, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps,
                         cv::Size(width, height));
  if (!writer.isOpened()) {
    std::cerr << "Could not create output video: " << outputPath << "\n";
    return 1;
  }

  int frameCount = 0;
  int detectedFrames = 0;
  cv::Mat frame;
  while (capture.read(frame)) {
    if (frame.empty()) {
      break;
    }

    cv::Mat mask;
    const auto bars = findLightbars(frame, mask);
    ArmorPair pair;
    if (bestPair(bars, pair)) {
      drawLightbar(frame, pair.left);
      drawLightbar(frame, pair.right);
      drawArmor(frame, pair);
      ++detectedFrames;
    }
    writer.write(frame);

    if (show) {
      cv::imshow("Armor lightbars", frame);
      if (cv::waitKey(1) == 27) {
        break;
      }
    }
    ++frameCount;
  }

  writer.release();
  capture.release();
  if (show) {
    cv::destroyAllWindows();
  }

  std::cout << "Frames processed: " << frameCount << "\n"
            << "Frames with an armor pair: " << detectedFrames << "\n"
            << "Saved: " << outputPath << "\n";
  return frameCount > 0 ? 0 : 1;
}
