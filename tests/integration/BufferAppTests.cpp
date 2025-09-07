#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>
#include <iterator>

namespace fs = std::filesystem;

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 64;
constexpr int kFrames = 1;
constexpr size_t kFrameSize = kWidth * kHeight * 3 / 2; // yuv420 8-bit

fs::path data_dir() {
    return fs::path(TEST_DATA_DIR);
}

fs::path override_dir() {
    return data_dir() / "override";
}

std::string read_cfg(const fs::path &path) {
    std::ifstream ifs(path);
    std::string content;
    std::getline(ifs, content);
    return content;
}

fs::path getenv_path(const char* name) {
    const char* v = std::getenv(name);
    if (v && *v) return fs::path(v);
    return {};
}

fs::path choose_cfg_path(const char* env_name,
                         const fs::path& override_candidate,
                         const fs::path& default_candidate) {
    fs::path from_env = getenv_path(env_name);
    if (!from_env.empty() && fs::exists(from_env)) return from_env;
    if (fs::exists(override_candidate)) return override_candidate;
    return default_candidate;
}

void write_yuv(const fs::path &path) {
    std::vector<uint8_t> zeros(kFrameSize, 0);
    std::ofstream ofs(path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(zeros.data()), zeros.size());
}

bool files_equal(const fs::path &a, const fs::path &b) {
    std::ifstream fa(a, std::ios::binary);
    std::ifstream fb(b, std::ios::binary);
    std::vector<char> da((std::istreambuf_iterator<char>(fa)), {});
    std::vector<char> db((std::istreambuf_iterator<char>(fb)), {});
    return da == db;
}

int run_cmd(const std::string &cmd) {
    return std::system(cmd.c_str());
}

} // namespace

TEST(BufferApps, EncodeMatch) {
    fs::path tmp = fs::temp_directory_path() / "buffer_encode_test";
    fs::create_directories(tmp);
    const fs::path override_input = override_dir() / "input.yuv";
    const bool use_override_input = fs::exists(override_input);
    fs::path input = use_override_input ? override_input : (tmp / "input.yuv");
    fs::path out1 = tmp / "enc.jxs";
    fs::path out2 = tmp / "enc_buf.jxs";
    if (!use_override_input) {
        write_yuv(input);
    }

    fs::path enc_cfg_path = choose_cfg_path(
        "BUFFER_TEST_ENC_CFG",
        override_dir() / "enc.cfg",
        data_dir() / "enc.cfg");

    if (use_override_input) {
        // When using large override input, require matching override enc.cfg unless env provided
        fs::path env_cfg = getenv_path("BUFFER_TEST_ENC_CFG");
        if (env_cfg.empty()) {
            ASSERT_TRUE(fs::exists(override_dir() / "enc.cfg"))
                << "Expected override enc.cfg at " << (override_dir() / "enc.cfg").string();
        }
    }

    std::string cfg_args = read_cfg(enc_cfg_path);
    std::cout << "EncodeMatch: input='" << input.string() << "' cfg='" << enc_cfg_path.string() << "'";
    if (use_override_input) std::cout << " (override)";
    std::cout << std::endl;
    fs::path enc_app = fs::path(".") / "SvtJpegxsEncApp";
    fs::path enc_app_buf = fs::path(".") / "EncAppBuffer";
    ASSERT_EQ(0, run_cmd(enc_app.string() + " " + cfg_args + " -i \"" + input.string() + "\" -b \"" + out1.string() + "\""));
    ASSERT_EQ(0, run_cmd(enc_app_buf.string() + " " + cfg_args + " -i \"" + input.string() + "\" -b \"" + out2.string() + "\""));
    EXPECT_TRUE(files_equal(out1, out2));
}

TEST(BufferApps, DecodeMatch) {
    fs::path tmp = fs::temp_directory_path() / "buffer_decode_test";
    fs::create_directories(tmp);
    const fs::path override_input = override_dir() / "input.yuv";
    const fs::path override_bitstream = override_dir() / "stream.jxs";
    const bool use_override_stream = fs::exists(override_bitstream);
    const bool use_override_input = fs::exists(override_input);
    fs::path input = use_override_input ? override_input : (tmp / "input.yuv");
    fs::path bitstream = use_override_stream ? override_bitstream : (tmp / "stream.jxs");
    fs::path out1 = tmp / "dec.yuv";
    fs::path out2 = tmp / "dec_buf.yuv";
    if (!use_override_input && !use_override_stream) {
        // Only need a generated input when we will encode a stream
        write_yuv(input);
    }

    // If we need to generate a bitstream, pick config (require override if using override input)
    fs::path enc_cfg_path = choose_cfg_path(
        "BUFFER_TEST_ENC_CFG",
        override_dir() / "enc.cfg",
        data_dir() / "enc.cfg");
    fs::path enc_app = fs::path(".") / "SvtJpegxsEncApp";
    fs::path dec_app = fs::path(".") / "SvtJpegxsDecApp";
    fs::path dec_app_buf = fs::path(".") / "DecAppBuffer";
    if (!use_override_stream) {
        if (use_override_input) {
            fs::path env_cfg = getenv_path("BUFFER_TEST_ENC_CFG");
            if (env_cfg.empty()) {
                ASSERT_TRUE(fs::exists(override_dir() / "enc.cfg"))
                    << "Expected override enc.cfg at " << (override_dir() / "enc.cfg").string();
            }
        }
        std::string enc_cfg = read_cfg(enc_cfg_path);
        ASSERT_EQ(0, run_cmd(enc_app.string() + " " + enc_cfg + " -i \"" + input.string() + "\" -b \"" + bitstream.string() + "\""));
    }

    fs::path dec_cfg_path = choose_cfg_path(
        "BUFFER_TEST_DEC_CFG",
        override_dir() / "dec.cfg",
        data_dir() / "dec.cfg");

    if (use_override_stream) {
        fs::path env_cfg = getenv_path("BUFFER_TEST_DEC_CFG");
        if (env_cfg.empty()) {
            ASSERT_TRUE(fs::exists(override_dir() / "dec.cfg"))
                << "Expected override dec.cfg at " << (override_dir() / "dec.cfg").string();
        }
    }

    std::string dec_cfg = read_cfg(dec_cfg_path);
    std::cout << "DecodeMatch: stream='" << bitstream.string() << "' cfg='" << dec_cfg_path.string() << "'";
    if (use_override_stream) std::cout << " (override)";
    std::cout << std::endl;
    ASSERT_EQ(0, run_cmd(dec_app.string() + " " + dec_cfg + " -i \"" + bitstream.string() + "\" -o \"" + out1.string() + "\""));
    ASSERT_EQ(0, run_cmd(dec_app_buf.string() + " " + dec_cfg + " -i \"" + bitstream.string() + "\" -o \"" + out2.string() + "\""));
    EXPECT_TRUE(files_equal(out1, out2));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

