#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// Đọc toàn bộ file vào một std::string
static std::string readFileToString(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Khong mo duoc file: " + path);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Tìm nút theo tên file template (vd: "button_01.png")
// và trả về center_x, center_y từ detected_buttons.json.
//
// Hàm này không dùng thư viện JSON, chỉ parse chuỗi đơn giản
// dựa trên format do detect_buttons.cpp ghi ra.
static bool findButtonCenter(
    const std::string& jsonText,
    const std::string& buttonName,
    int& outCx,
    int& outCy
) {
    const std::string tplKey = "\"template\":";
    std::size_t pos = 0;

    while (true) {
        // Tìm "template" tiếp theo
        std::size_t tPos = jsonText.find(tplKey, pos);
        if (tPos == std::string::npos) {
            break;
        }

        // Tìm dấu nháy mở của giá trị template
        std::size_t quote1 = jsonText.find('"', tPos + tplKey.size());
        if (quote1 == std::string::npos) break;
        std::size_t quote2 = jsonText.find('"', quote1 + 1);
        if (quote2 == std::string::npos) break;

        std::string tplValue = jsonText.substr(quote1 + 1, quote2 - quote1 - 1);

        // Kiểm tra xem có chứa tên button mong muốn (button_01.png, v.v.)
        if (tplValue.find(buttonName) != std::string::npos) {
            // Từ vị trí này trở đi, tìm "center_x" và "center_y"
            const std::string cxKey = "\"center_x\":";
            const std::string cyKey = "\"center_y\":";

            std::size_t cxPos = jsonText.find(cxKey, quote2);
            std::size_t cyPos = jsonText.find(cyKey, quote2);
            if (cxPos == std::string::npos || cyPos == std::string::npos) {
                return false;
            }

            cxPos += cxKey.size();
            cyPos += cyKey.size();

            // Đơn giản: dùng std::stoi từ vị trí sau key
            try {
                outCx = std::stoi(jsonText.substr(cxPos));
                outCy = std::stoi(jsonText.substr(cyPos));
            } catch (...) {
                return false;
            }
            return true;
        }

        pos = quote2 + 1;
    }

    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n";
        std::cerr << "  ./click_button <button_name> [json_path]\n";
        std::cerr << "Vi du:\n";
        std::cerr << "  ./click_button button_01.png\n";
        return 1;
    }

    const std::string buttonName = argv[1];               // vd: "button_01.png"
    const std::string jsonPath   = (argc >= 3) ? argv[2]  // tuỳ chọn
                                               : "detected_buttons.json";

    std::string jsonText;
    try {
        jsonText = readFileToString(jsonPath);
    } catch (const std::exception& e) {
        std::cerr << "Loi: " << e.what() << std::endl;
        return 1;
    }

    int cx = 0, cy = 0;
    if (!findButtonCenter(jsonText, buttonName, cx, cy)) {
        std::cerr << "Khong tim thay button '" << buttonName
                  << "' trong file JSON: " << jsonPath << std::endl;
        return 1;
    }

    std::cout << "Button '" << buttonName << "' tai toa do: (" << cx << ", " << cy << ")\n";

    // Gửi lệnh tap qua ADB. Ở đây dùng `adb shell input tap`.
    // Có thể mở rộng sau để nhận --device ... nếu cần.
    std::string cmd = "adb shell input tap " + std::to_string(cx) + " " + std::to_string(cy);
    std::cout << "Chay lenh: " << cmd << std::endl;

    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "Lenh ADB that bai, ma thoat = " << rc << std::endl;
        return 1;
    }

    return 0;
}

