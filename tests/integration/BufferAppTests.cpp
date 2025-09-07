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

fs::path find_override(const char *env_var, const std::string &name) {
    if (const char *env = std::getenv(env_var); env && fs::exists(env)) {
        return fs::path(env);
    }
    fs::path override = data_dir() / "override" / name;
    if (fs::exists(override)) {
        return override;
    }
    return {};
}

std::string read_cfg(const fs::path &path) {
    std::ifstream ifs(path);
    std::string content((std::istreambuf_iterator<char>(ifs)), {});
    for (char &c : content) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return content;
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
    fs::path input_override = find_override("BUFFER_TEST_INPUT_YUV", "input.yuv");
    fs::path input = input_override.empty() ? tmp / "input.yuv" : input_override;
    if (input_override.empty()) {
        write_yuv(input);
    }
    fs::path out1 = tmp / "enc.jxs";
    fs::path out2 = tmp / "enc_buf.jxs";

    fs::path enc_cfg_override = find_override("BUFFER_TEST_ENC_CFG", "enc.cfg");
    fs::path enc_cfg_path;
    if (!input_override.empty()) {
        if (!enc_cfg_override.empty()) {
            enc_cfg_path = enc_cfg_override;
        } else {
            enc_cfg_path = input.parent_path() / "enc.cfg";
            ASSERT_TRUE(fs::exists(enc_cfg_path)) << "enc.cfg not found alongside overridden input";
        }
    } else {
        enc_cfg_path = !enc_cfg_override.empty() ? enc_cfg_override : data_dir() / "enc.cfg";
    }
    std::string cfg_args = read_cfg(enc_cfg_path);
    fs::path enc_app = fs::path(".") / "SvtJpegxsEncApp";
    fs::path enc_app_buf = fs::path(".") / "EncAppBuffer";
    std::string cmd1 = enc_app.string() + " " + cfg_args + " -i \"" + input.string() + "\" -b \"" + out1.string() + "\"";
    ASSERT_EQ(0, run_cmd(cmd1));
    std::string cmd2 = enc_app_buf.string() + " " + cfg_args + " -i \"" + input.string() + "\" -b \"" + out2.string() + "\"";
    ASSERT_EQ(0, run_cmd(cmd2));
    EXPECT_TRUE(files_equal(out1, out2));
}

TEST(BufferApps, DecodeMatch) {
    fs::path tmp = fs::temp_directory_path() / "buffer_decode_test";
    fs::create_directories(tmp);
    fs::path stream_override = find_override("BUFFER_TEST_STREAM_JXS", "stream.jxs");
    bool use_stream_override = !stream_override.empty();
    fs::path bitstream = use_stream_override ? stream_override : tmp / "stream.jxs";

    if (!use_stream_override) {
        fs::path input_override = find_override("BUFFER_TEST_INPUT_YUV", "input.yuv");
        fs::path input = input_override.empty() ? tmp / "input.yuv" : input_override;
        if (input_override.empty()) {
            write_yuv(input);
        }
        fs::path enc_cfg_override = find_override("BUFFER_TEST_ENC_CFG", "enc.cfg");
        fs::path enc_cfg_path;
        if (!input_override.empty()) {
            if (!enc_cfg_override.empty()) {
                enc_cfg_path = enc_cfg_override;
            } else {
                enc_cfg_path = input.parent_path() / "enc.cfg";
                ASSERT_TRUE(fs::exists(enc_cfg_path)) << "enc.cfg not found alongside overridden input";
            }
        } else {
            enc_cfg_path = !enc_cfg_override.empty() ? enc_cfg_override : data_dir() / "enc.cfg";
        }
        std::string enc_cfg = read_cfg(enc_cfg_path);
        fs::path enc_app = fs::path(".") / "SvtJpegxsEncApp";
        std::string enc_cmd = enc_app.string() + " " + enc_cfg + " -i \"" + input.string() + "\" -b \"" + bitstream.string() + "\"";
        ASSERT_EQ(0, run_cmd(enc_cmd));
    }

    fs::path out1 = tmp / "dec.yuv";
    fs::path out2 = tmp / "dec_buf.yuv";
    fs::path dec_cfg_override = find_override("BUFFER_TEST_DEC_CFG", "dec.cfg");
    fs::path dec_cfg_path;
    if (use_stream_override) {
        if (!dec_cfg_override.empty()) {
            dec_cfg_path = dec_cfg_override;
        } else {
            dec_cfg_path = bitstream.parent_path() / "dec.cfg";
            ASSERT_TRUE(fs::exists(dec_cfg_path)) << "dec.cfg not found alongside overridden stream";
        }
    } else {
        dec_cfg_path = !dec_cfg_override.empty() ? dec_cfg_override : data_dir() / "dec.cfg";
    }
    std::string dec_cfg = read_cfg(dec_cfg_path);
    fs::path dec_app = fs::path(".") / "SvtJpegxsDecApp";
    fs::path dec_app_buf = fs::path(".") / "DecAppBuffer";
    std::string dec_cmd1 = dec_app.string() + " " + dec_cfg + " -i \"" + bitstream.string() + "\" -o \"" + out1.string() + "\"";
    ASSERT_EQ(0, run_cmd(dec_cmd1));
    std::string dec_cmd2 = dec_app_buf.string() + " " + dec_cfg + " -i \"" + bitstream.string() + "\" -o \"" + out2.string() + "\"";
    ASSERT_EQ(0, run_cmd(dec_cmd2));
    EXPECT_TRUE(files_equal(out1, out2));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

