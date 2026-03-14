#pragma once

#include <opencv2/opencv.hpp>
#include <string>

// Capture Android screen via ADB and decode to cv::Mat (BGR).
// Requires: adb in PATH, USB debugging enabled, device authorized.
//
// deviceSerial:
//   - empty: use default device selected by adb
//   - non-empty: passed as `adb -s <serial> ...`
cv::Mat captureAndroidScreenBgr(const std::string& deviceSerial = "");

