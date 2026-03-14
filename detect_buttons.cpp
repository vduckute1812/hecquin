#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iostream>
#include <string>

#include "adb_screencap.hpp"
namespace fs = std::filesystem;

// Tìm tất cả template trong thư mục button_templates và match trên ảnh main_screen.jpeg
int main(int argc, char** argv) {
    std::string screenPath  = "images/main_screen.jpeg";   // ảnh màn hình cần tìm nút
    std::string templateDir = "button_templates";          // thư mục chứa các button_XX.png
    std::string debugOut    = "debug_detect_result.png";   // ảnh vẽ khung kết quả

    bool useAdb = false;
    std::string deviceSerial;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--adb") {
            useAdb = true;
        } else if (a == "--screen" && i + 1 < argc) {
            screenPath = argv[++i];
        } else if (a == "--templates" && i + 1 < argc) {
            templateDir = argv[++i];
        } else if (a == "--debug-out" && i + 1 < argc) {
            debugOut = argv[++i];
        } else if (a == "--device" && i + 1 < argc) {
            deviceSerial = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::cout
                << "Usage:\n"
                << "  ./detect_buttons [--screen <path>] [--templates <dir>] [--debug-out <path>]\n"
                << "  ./detect_buttons --adb [--device <serial>] [--templates <dir>] [--debug-out <path>]\n";
            return 0;
        }
    }

    // Đọc ảnh màn hình
    cv::Mat screen;
    try {
        if (useAdb) {
            screen = captureAndroidScreenBgr(deviceSerial);
        } else {
            screen = cv::imread(screenPath);
        }
    } catch (const std::exception& e) {
        std::cerr << "Loi khi lay anh man hinh: " << e.what() << std::endl;
        return 1;
    }
    if (screen.empty()) {
        std::cerr << "Khong doc duoc anh man hinh: " << screenPath << std::endl;
        return 1;
    }

    if (!fs::exists(templateDir)) {
        std::cerr << "Thu muc template khong ton tai: " << templateDir << std::endl;
        std::cerr << "Hay chay ./extract_buttons truoc de tao cac file template." << std::endl;
        return 1;
    }

    // Sao chép ảnh để vẽ khung
    cv::Mat vis = screen.clone();

    // Ngưỡng tương đồng (0.0–1.0). Giá trị càng cao => match càng chính xác.
    const double threshold = 0.8;

    for (const auto &entry : fs::directory_iterator(templateDir)) {
        if (!entry.is_regular_file()) continue;

        std::string tplPath = entry.path().string();
        cv::Mat tpl = cv::imread(tplPath);
        if (tpl.empty()) {
            std::cerr << "Khong doc duoc template: " << tplPath << std::endl;
            continue;
        }

        // Thực hiện template matching
        cv::Mat result;
        int resultCols = screen.cols - tpl.cols + 1;
        int resultRows = screen.rows - tpl.rows + 1;
        if (resultCols <= 0 || resultRows <= 0) {
            std::cerr << "Template lon hon anh man hinh, bo qua: " << tplPath << std::endl;
            continue;
        }
        result.create(resultRows, resultCols, CV_32FC1);

        // Sử dụng phương pháp TM_CCOEFF_NORMED (giá trị max gần 1 là giống)
        cv::matchTemplate(screen, tpl, result, cv::TM_CCOEFF_NORMED);
        cv::normalize(result, result, 0, 1, cv::NORM_MINMAX, -1, cv::Mat());

        // Tìm vị trí có độ tương đồng cao nhất
        double minVal, maxVal;
        cv::Point minLoc, maxLoc;
        cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

        std::cout << "Template: " << tplPath << " | maxVal = " << maxVal << std::endl;

        if (maxVal >= threshold) {
            // Vẽ hình chữ nhật quanh vị trí match
            cv::Rect matchRect(maxLoc.x, maxLoc.y, tpl.cols, tpl.rows);
            cv::rectangle(vis, matchRect, cv::Scalar(0, 0, 255), 2);

            // In toạ độ tâm nút (dùng để tap trên điện thoại)
            int centerX = matchRect.x + matchRect.width / 2;
            int centerY = matchRect.y + matchRect.height / 2;
            std::cout << "  -> Phat hien o: (" << centerX << ", " << centerY << ")" << std::endl;
        } else {
            std::cout << "  -> Khong dat nguong, bo qua." << std::endl;
        }
    }

    // Lưu ảnh debug có vẽ khung
    if (cv::imwrite(debugOut, vis)) {
        std::cout << "Da luu anh debug: " << debugOut << std::endl;
    } else {
        std::cerr << "Khong luu duoc anh debug." << std::endl;
    }

    return 0;
}

