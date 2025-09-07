#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

namespace fs = std::filesystem;

static fs::path data_dir() {
    return fs::path(TEST_DATA_DIR);
}

static fs::path override_dir() { return data_dir() / "override"; }

static std::string read_cfg_line(const fs::path &path) {
    std::ifstream ifs(path);
    std::string content;
    std::getline(ifs, content);
    return content;
}

static fs::path getenv_path(const char* name) {
    const char* v = std::getenv(name);
    if (v && *v) return fs::path(v);
    return {};
}

static fs::path choose_cfg_path(const char* env_name,
                                const fs::path& override_candidate,
                                const fs::path& default_candidate) {
    fs::path from_env = getenv_path(env_name);
    if (!from_env.empty() && fs::exists(from_env)) return from_env;
    if (fs::exists(override_candidate)) return override_candidate;
    return default_candidate;
}

static int run_cmd(const std::string &cmd) { return std::system(cmd.c_str()); }

static bool files_equal_streaming(const fs::path &a, const fs::path &b, size_t buf = 1 << 20) {
    if (!fs::exists(a) || !fs::exists(b)) return false;
    if (fs::file_size(a) != fs::file_size(b)) return false;
    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    std::vector<char> da(buf), db(buf);
    while (fa && fb) {
        fa.read(da.data(), da.size());
        fb.read(db.data(), db.size());
        std::streamsize ra = fa.gcount();
        std::streamsize rb = fb.gcount();
        if (ra != rb) return false;
        if (ra == 0) break;
        if (std::memcmp(da.data(), db.data(), static_cast<size_t>(ra)) != 0) return false;
    }
    return true;
}

int main() {
    // Expect override files to exist for full-run scenario
    fs::path ovr = override_dir();
    fs::path in = ovr / "input.yuv";
    fs::path bs = ovr / "stream.jxs";
    if (!fs::exists(in)) {
        std::cerr << "Missing override input: " << in << std::endl;
        return 2;
    }
    if (!fs::exists(bs)) {
        std::cerr << "Missing override bitstream: " << bs << std::endl;
        return 2;
    }

    fs::path enc_cfg = choose_cfg_path("BUFFER_TEST_ENC_CFG", ovr / "enc.cfg", data_dir() / "enc.cfg");
    fs::path dec_cfg = choose_cfg_path("BUFFER_TEST_DEC_CFG", ovr / "dec.cfg", data_dir() / "dec.cfg");

    // Require override cfgs if env not provided
    if (getenv_path("BUFFER_TEST_ENC_CFG").empty() && enc_cfg != ovr / "enc.cfg") {
        std::cerr << "Expected override enc.cfg at " << (ovr / "enc.cfg") << std::endl;
        return 2;
    }
    if (getenv_path("BUFFER_TEST_DEC_CFG").empty() && dec_cfg != ovr / "dec.cfg") {
        std::cerr << "Expected override dec.cfg at " << (ovr / "dec.cfg") << std::endl;
        return 2;
    }

    std::string enc_args = read_cfg_line(enc_cfg);
    std::string dec_args = read_cfg_line(dec_cfg);

    fs::path work = fs::temp_directory_path() / "buffer_apps_runner";
    fs::create_directories(work);
    fs::path enc1 = work / "enc_app.jxs";
    fs::path enc2 = work / "enc_buf.jxs";
    fs::path dec1 = work / "dec_app.yuv";
    fs::path dec2 = work / "dec_buf.yuv";

    fs::path enc_app = fs::path(".") / "SvtJpegxsEncApp";
    fs::path enc_app_buf = fs::path(".") / "EncAppBuffer";
    fs::path dec_app = fs::path(".") / "SvtJpegxsDecApp";
    fs::path dec_app_buf = fs::path(".") / "DecAppBuffer";

    std::cout << "Encode: input='" << in.string() << "' cfg='" << enc_cfg.string() << "'" << std::endl;
    if (run_cmd(enc_app.string() + " " + enc_args + " -i \"" + in.string() + "\" -b \"" + enc1.string() + "\"")) {
        std::cerr << "SvtJpegxsEncApp failed" << std::endl;
        return 3;
    }
    if (run_cmd(enc_app_buf.string() + " " + enc_args + " -i \"" + in.string() + "\" -b \"" + enc2.string() + "\"")) {
        std::cerr << "EncAppBuffer failed" << std::endl;
        return 3;
    }
    if (!files_equal_streaming(enc1, enc2)) {
        std::cerr << "Encoded bitstreams differ" << std::endl;
        return 4;
    }
    std::cout << "Encode: outputs identical, size=" << fs::file_size(enc1) << std::endl;

    std::cout << "Decode: stream='" << bs.string() << "' cfg='" << dec_cfg.string() << "'" << std::endl;
    if (run_cmd(dec_app.string() + " " + dec_args + " -i \"" + bs.string() + "\" -o \"" + dec1.string() + "\"")) {
        std::cerr << "SvtJpegxsDecApp failed" << std::endl;
        return 5;
    }
    if (run_cmd(dec_app_buf.string() + " " + dec_args + " -i \"" + bs.string() + "\" -o \"" + dec2.string() + "\"")) {
        std::cerr << "DecAppBuffer failed" << std::endl;
        return 5;
    }
    if (!files_equal_streaming(dec1, dec2)) {
        std::cerr << "Decoded outputs differ" << std::endl;
        return 6;
    }
    std::cout << "Decode: outputs identical, size=" << fs::file_size(dec1) << std::endl;
    return 0;
}

