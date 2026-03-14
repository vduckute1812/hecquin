#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iostream>

#include <string>

#include "adb_screencap.hpp"
namespace fs = std::filesystem;

int main(int argc, char** argv) {
    // Đường dẫn ảnh gốc: dùng trực tiếp file screenshot trong workspace Cursor
    // Bạn có thể copy file này vào thư mục project nếu muốn đường dẫn tương đối.
    std::string inputImage = "images/main_screen.png";
    std::string outputDir  = "button_templates"; // thư mục con ngay trong project

    bool useAdb = false;
    std::string deviceSerial;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--input" && i + 1 < argc) {
            inputImage = argv[++i];
        } else if (a == "--adb") {
            useAdb = true;
        } else if (a == "--device" && i + 1 < argc) {
            deviceSerial = argv[++i];
        } else if (a == "--output-dir" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::cout
                << "Usage:\n"
                << "  ./extract_buttons --input <path> [--output-dir <dir>]\n"
                << "  ./extract_buttons --adb [--device <serial>] [--output-dir <dir>]\n";
            return 0;
        }
    }

    // Tạo thư mục nếu chưa tồn tại (trong /home/duc13t3/Projects/hecquin)
    try {
        fs::create_directories(outputDir);
    } catch (const std::exception &e) {
        std::cerr << "Khong tao duoc thu muc '" << outputDir << "': " << e.what() << std::endl;
        return 1;
    }

    // Đọc ảnh
    cv::Mat img;
    try {
        if (useAdb) {
            img = captureAndroidScreenBgr(deviceSerial);
        } else {
            img = cv::imread(inputImage);
        }
    } catch (const std::exception& e) {
        std::cerr << "Loi khi lay anh: " << e.what() << std::endl;
        return 1;
    }
    if (img.empty()) {
        std::cerr << "Khong doc duoc anh: " << inputImage << std::endl;
        return 1;
    }

    std::cout << "Dung chuot trai keo de chon cac nut." << std::endl;
    std::cout << "- Nhan ENTER khi chon xong." << std::endl;
    std::cout << "- Nhan ESC de huy." << std::endl;

    // Cho phép chọn nhiều ROI (Region of Interest)
    std::vector<cv::Rect> rois;
    cv::namedWindow("Chon cac nut tren man hinh", cv::WINDOW_NORMAL);
    // OpenCV 4: selectROIs trả về void, nhận vector<Rect>& làm tham số thứ 3
    cv::selectROIs("Chon cac nut tren man hinh", img, rois, true, false);
    cv::destroyAllWindows();

    if (rois.empty()) {
        std::cout << "Khong co vung nao duoc chon." << std::endl;
        return 0;
    }

    std::cout << "Da chon " << rois.size() << " vung. Dang luu anh..." << std::endl;

    int idx = 1;
    for (const auto &r : rois) {
        // Đảm bảo ROI không vượt ra ngoài ảnh
        cv::Rect rect = r & cv::Rect(0, 0, img.cols, img.rows);
        if (rect.width <= 0 || rect.height <= 0) {
            continue;
        }

        cv::Mat crop = img(rect);

        char filename[64];
        std::snprintf(filename, sizeof(filename), "button_%02d.png", idx++);
        fs::path outPath = fs::path(outputDir) / filename;

        if (!cv::imwrite(outPath.string(), crop)) {
            std::cerr << "Luu that bai: " << outPath << std::endl;
        } else {
            std::cout << "Da luu: " << outPath << std::endl;
        }
    }

    std::cout << "Hoan tat cat nut bam." << std::endl;
    return 0;
}

