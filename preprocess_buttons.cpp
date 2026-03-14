#include <opencv2/opencv.hpp>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    std::string inputDir  = "button_templates";
    std::string outputDir = "button_templates_preprocessed";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--input-dir" && i + 1 < argc) {
            inputDir = argv[++i];
        } else if (a == "--output-dir" && i + 1 < argc) {
            outputDir = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::cout
                << "Usage:\n"
                << "  ./preprocess_buttons [--input-dir <dir>] [--output-dir <dir>]\n\n"
                << "Mac dinh:\n"
                << "  --input-dir  button_templates\n"
                << "  --output-dir button_templates_preprocessed\n";
            return 0;
        }
    }

    if (!fs::exists(inputDir)) {
        std::cerr << "Thu muc input khong ton tai: " << inputDir << std::endl;
        std::cerr << "Hay chay ./extract_buttons truoc de tao cac template." << std::endl;
        return 1;
    }

    try {
        fs::create_directories(outputDir);
    } catch (const std::exception& e) {
        std::cerr << "Khong tao duoc thu muc output '" << outputDir << "': " << e.what()
                  << std::endl;
        return 1;
    }

    std::cout << "Tien xu ly cac button trong: " << inputDir << std::endl;
    std::cout << "Luu ket qua vao: " << outputDir << std::endl;

    int processedCount = 0;

    for (const auto& entry : fs::directory_iterator(inputDir)) {
        if (!entry.is_regular_file()) continue;

        const auto& path = entry.path();
        const std::string ext = path.extension().string();
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") {
            continue;
        }

        cv::Mat src = cv::imread(path.string(), cv::IMREAD_COLOR);
        if (src.empty()) {
            std::cerr << "Khong doc duoc anh: " << path << std::endl;
            continue;
        }

        // Chuyen sang grayscale va lam mo nhe de giam nhieu
        cv::Mat gray;
        cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, cv::Size(3, 3), 0);

        // Luu anh mot kenh (grayscale) voi cung ten file trong thu muc output
        fs::path outPath = fs::path(outputDir) / path.filename();
        if (!cv::imwrite(outPath.string(), gray)) {
            std::cerr << "Khong luu duoc anh tien xu ly: " << outPath << std::endl;
            continue;
        }

        std::cout << "Da tien xu ly: " << path.filename().string()
                  << " -> " << outPath.string() << std::endl;
        ++processedCount;
    }

    std::cout << "Hoan tat. So file da xu ly: " << processedCount << std::endl;
    return 0;
}

