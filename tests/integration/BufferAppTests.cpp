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

std::string read_cfg(const fs::path &path) {
    std::ifstream ifs(path);
    std::string content;
    std::getline(ifs, content);
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
    fs::path input = tmp / "input.yuv";
    fs::path out1 = tmp / "enc.jxs";
    fs::path out2 = tmp / "enc_buf.jxs";
    write_yuv(input);
    std::string cfg_args = read_cfg(data_dir() / "enc.cfg");
    fs::path enc_app = fs::path(".") / "SvtJpegxsEncApp";
    fs::path enc_app_buf = fs::path(".") / "EncAppBuffer";
    ASSERT_EQ(0, run_cmd(enc_app.string() + " " + cfg_args + " -i \"" + input.string() + "\" -b \"" + out1.string() + "\""));
    ASSERT_EQ(0, run_cmd(enc_app_buf.string() + " " + cfg_args + " -i \"" + input.string() + "\" -b \"" + out2.string() + "\""));
    EXPECT_TRUE(files_equal(out1, out2));
}

TEST(BufferApps, DecodeMatch) {
    fs::path tmp = fs::temp_directory_path() / "buffer_decode_test";
    fs::create_directories(tmp);
    fs::path input = tmp / "input.yuv";
    fs::path bitstream = tmp / "stream.jxs";
    fs::path out1 = tmp / "dec.yuv";
    fs::path out2 = tmp / "dec_buf.yuv";
    write_yuv(input);
    std::string enc_cfg = read_cfg(data_dir() / "enc.cfg");
    fs::path enc_app = fs::path(".") / "SvtJpegxsEncApp";
    fs::path dec_app = fs::path(".") / "SvtJpegxsDecApp";
    fs::path dec_app_buf = fs::path(".") / "DecAppBuffer";
    ASSERT_EQ(0, run_cmd(enc_app.string() + " " + enc_cfg + " -i \"" + input.string() + "\" -b \"" + bitstream.string() + "\""));
    std::string dec_cfg = read_cfg(data_dir() / "dec.cfg");
    ASSERT_EQ(0, run_cmd(dec_app.string() + " " + dec_cfg + " -i \"" + bitstream.string() + "\" -o \"" + out1.string() + "\""));
    ASSERT_EQ(0, run_cmd(dec_app_buf.string() + " " + dec_cfg + " -i \"" + bitstream.string() + "\" -o \"" + out2.string() + "\""));
    EXPECT_TRUE(files_equal(out1, out2));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

