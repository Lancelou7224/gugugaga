#include <opencv2/opencv.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr int kSaturationMin = 100;
constexpr int kValueMin = 70;

cv::Mat redMask(const cv::Mat & hsv)
{
  cv::Mat lowHue;
  cv::Mat highHue;
  cv::Mat mask;

  cv::inRange(hsv, cv::Scalar(0, kSaturationMin, kValueMin), cv::Scalar(10, 255, 255), lowHue);
  cv::inRange(hsv, cv::Scalar(170, kSaturationMin, kValueMin), cv::Scalar(180, 255, 255), highHue);
  cv::bitwise_or(lowHue, highHue, mask);

  const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
  cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
  return mask;
}

cv::Mat titled(const cv::Mat & image, const std::string & title)
{
  cv::Mat color;
  if (image.channels() == 1) {
    cv::cvtColor(image, color, cv::COLOR_GRAY2BGR);
  } else {
    color = image.clone();
  }

  cv::Mat panel(color.rows + 32, color.cols, CV_8UC3, cv::Scalar(35, 35, 35));
  color.copyTo(panel(cv::Rect(0, 32, color.cols, color.rows)));
  cv::putText(panel, title, cv::Point(10, 23), cv::FONT_HERSHEY_SIMPLEX, 0.65,
              cv::Scalar(240, 240, 240), 1, cv::LINE_AA);
  return panel;
}
}  // namespace

int main(int argc, char ** argv)
{
  std::string imagePath = "assets/armor.jpg";
  bool show = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--image" && i + 1 < argc) {
      imagePath = argv[++i];
    } else if (arg == "--show") {
      show = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: hsv_demo [--image path] [--show]\n";
      return 0;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      return 2;
    }
  }

  const cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
  if (image.empty()) {
    std::cerr << "Could not read image: " << imagePath << "\n";
    return 1;
  }

  cv::Mat hsv;
  cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
  std::vector<cv::Mat> channels;
  cv::split(hsv, channels);
  const cv::Mat mask = redMask(hsv);

  if (!cv::imwrite("red_mask.png", mask)) {
    std::cerr << "Could not save red_mask.png\n";
    return 1;
  }

  cv::Mat overlay = image.clone();
  overlay.setTo(cv::Scalar(0, 0, 255), mask);
  cv::Mat result;
  cv::hconcat(std::vector<cv::Mat>{
                titled(image, "Original"), titled(channels[0], "H"), titled(channels[1], "S")},
              result);
  cv::Mat bottom;
  cv::hconcat(std::vector<cv::Mat>{
                titled(channels[2], "V"), titled(mask, "Red mask"), titled(overlay, "Mask on image")},
              bottom);
  cv::vconcat(result, bottom, result);

  if (!cv::imwrite("task1_result.png", result)) {
    std::cerr << "Could not save task1_result.png\n";
    return 1;
  }

  std::cout << "Image: " << image.cols << "x" << image.rows << "\n"
            << "Red pixels: " << cv::countNonZero(mask) << "\n"
            << "Saved red_mask.png and task1_result.png\n";

  if (show) {
    cv::imshow("H channel", channels[0]);
    cv::imshow("S channel", channels[1]);
    cv::imshow("V channel", channels[2]);
    cv::imshow("Red mask", mask);
    cv::waitKey(0);
    cv::destroyAllWindows();
  }
  return 0;
}
