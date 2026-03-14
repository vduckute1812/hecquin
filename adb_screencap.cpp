#include "adb_screencap.hpp"

#include <cstdio>
#include <array>
#include <stdexcept>
#include <vector>

static std::string buildAdbCommand(const std::string& deviceSerial) {
    std::string cmd = "adb ";
    if (!deviceSerial.empty()) {
        cmd += "-s ";
        cmd += deviceSerial;
        cmd += " ";
    }
    cmd += "exec-out screencap -p";
    return cmd;
}

cv::Mat captureAndroidScreenBgr(const std::string& deviceSerial) {
    const std::string cmd = buildAdbCommand(deviceSerial);

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Khong mo duoc pipe de chay adb. Hay cai adb va thu lai.");
    }

    std::vector<uchar> bytes;
    bytes.reserve(2 * 1024 * 1024);

    std::array<unsigned char, 1 << 15> buf{};
    while (true) {
        const size_t n = std::fread(buf.data(), 1, buf.size(), pipe);
        if (n > 0) {
            bytes.insert(bytes.end(), buf.data(), buf.data() + n);
        }
        if (n < buf.size()) {
            if (std::feof(pipe)) break;
            if (std::ferror(pipe)) break;
        }
    }

    const int rc = pclose(pipe);
    if (rc != 0) {
        throw std::runtime_error("ADB that bai. Kiem tra: da bat USB debugging, da authorize, va `adb devices` co thay thiet bi.");
    }
    if (bytes.empty()) {
        throw std::runtime_error("Khong nhan duoc du lieu anh tu ADB.");
    }

    cv::Mat img = cv::imdecode(bytes, cv::IMREAD_COLOR);
    if (img.empty()) {
        throw std::runtime_error("Khong decode duoc PNG tu ADB (imdecode tra ve empty).");
    }
    return img;
}

