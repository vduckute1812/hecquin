// Regression for issue #1 + #4: AppConfig::load must resolve every relative
// path in config.env against the config file's directory.  Previously the
// raw relative strings were handed back as-is, so `english_ingest`
// (cwd=sound/) and `english_tutor` (cwd=sound/build/<plat>/) opened two
// different files for the same `HECQUIN_LEARNING_DB_PATH=.env/.../learning.sqlite`.

#include "config/AppConfig.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int fail(const char* message) {
    std::cerr << "[test_app_config_path_resolution] FAIL: " << message << std::endl;
    return 1;
}

fs::path make_tmp_dir() {
    char buf[] = "/tmp/hecquin_appcfg_XXXXXX";
    if (!mkdtemp(buf)) return {};
    return fs::path(buf);
}

void write_file(const fs::path& p, const std::string& body) {
    std::ofstream out(p);
    out << body;
}

} // namespace

int main() {
    const fs::path root = make_tmp_dir();
    if (root.empty()) return fail("could not allocate tmp dir");

    const fs::path env_dir = root / ".env";
    fs::create_directories(env_dir);

    const std::string body =
        "HECQUIN_LEARNING_DB_PATH=.env/learning/db/learning.sqlite\n"
        "HECQUIN_LEARNING_CURRICULUM_DIR=.env/learning/curriculum\n"
        "HECQUIN_LEARNING_CUSTOM_DIR=.env/learning/custom\n"
        "HECQUIN_PRONUNCIATION_MODEL=/abs/path/to/model.onnx\n"
        "HECQUIN_PRONUNCIATION_VOCAB=.env/models/vocab.json\n"
        "HECQUIN_MUSIC_COOKIES_FILE=\n";

    const fs::path env_file = env_dir / "config.env";
    write_file(env_file, body);

    const auto cfg = AppConfig::load(env_file.string().c_str());

    // Relative paths must now be anchored at the env dir.
    const std::string expected_db =
        (env_dir / ".env/learning/db/learning.sqlite").lexically_normal().string();
    if (cfg.learning.db_path != expected_db) {
        std::cerr << "  got: " << cfg.learning.db_path << "\n  want: " << expected_db << "\n";
        fs::remove_all(root);
        return fail("relative db_path must resolve against config dir");
    }

    const std::string expected_curr =
        (env_dir / ".env/learning/curriculum").lexically_normal().string();
    if (cfg.learning.curriculum_dir != expected_curr) {
        fs::remove_all(root);
        return fail("relative curriculum_dir must resolve against config dir");
    }

    const std::string expected_custom =
        (env_dir / ".env/learning/custom").lexically_normal().string();
    if (cfg.learning.custom_dir != expected_custom) {
        fs::remove_all(root);
        return fail("relative custom_dir must resolve against config dir");
    }

    const std::string expected_vocab =
        (env_dir / ".env/models/vocab.json").lexically_normal().string();
    if (cfg.pronunciation.vocab_path != expected_vocab) {
        fs::remove_all(root);
        return fail("relative vocab_path must resolve against config dir");
    }

    // Absolute paths must pass through unchanged (only normalised).
    if (cfg.pronunciation.model_path != "/abs/path/to/model.onnx") {
        fs::remove_all(root);
        return fail("absolute model_path must be passed through unchanged");
    }

    // Empty values must stay empty.
    if (!cfg.music.cookies_file.empty()) {
        fs::remove_all(root);
        return fail("empty cookies_file must remain empty");
    }

    fs::remove_all(root);
    std::cout << "[test_app_config_path_resolution] all assertions passed" << std::endl;
    return 0;
}
