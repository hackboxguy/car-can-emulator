// Lightweight test harness for car-can-emulator
// No external dependencies — uses assert-style checks for cross-platform compatibility

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <fstream>
#include <string>
#include <map>

#include "../src/obd-encode.h"
#include "../src/config-utils.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct Register_##name { Register_##name() { test_##name(); } } reg_##name; \
    static void test_##name()

#define CHECK(expr) do { \
    tests_run++; \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    } else { \
        tests_passed++; \
    } \
} while(0)

// Helper: create a temp config file, return its path
static std::string write_temp_config(const std::string &content)
{
    char tmpl[] = "/tmp/test-conf-XXXXXX";
    int fd = mkstemp(tmpl);
    assert(fd >= 0);
    write(fd, content.c_str(), content.size());
    close(fd);
    return std::string(tmpl);
}

// ============================================================
// OBD Encoding Tests
// ============================================================

TEST(obd_speed)
{
    printf("  obd_speed encoding...\n");
    OBDResponse r = encode_obd_response(0x0D, 120);
    CHECK(r.respond == true);
    CHECK(r.data[0] == 0x03); // 3-byte payload
    CHECK(r.data[1] == 0x41); // Mode 01 response
    CHECK(r.data[2] == 0x0D); // PID
    CHECK(r.data[3] == 120);  // direct km/h
}

TEST(obd_speed_zero)
{
    printf("  obd_speed zero...\n");
    OBDResponse r = encode_obd_response(0x0D, 0);
    CHECK(r.respond == true);
    CHECK(r.data[3] == 0);
}

TEST(obd_speed_max)
{
    printf("  obd_speed max (255)...\n");
    OBDResponse r = encode_obd_response(0x0D, 255);
    CHECK(r.respond == true);
    CHECK(r.data[3] == 255);
}

TEST(obd_rpm)
{
    printf("  obd_rpm encoding (3000 RPM)...\n");
    OBDResponse r = encode_obd_response(0x0C, 3000);
    CHECK(r.respond == true);
    CHECK(r.data[0] == 0x04); // 4-byte payload
    CHECK(r.data[1] == 0x41);
    CHECK(r.data[2] == 0x0C);
    // 3000 * 4 = 12000 = 0x2EE0
    CHECK(r.data[3] == 0x2E);
    CHECK(r.data[4] == 0xE0);
}

TEST(obd_rpm_idle)
{
    printf("  obd_rpm idle (800 RPM)...\n");
    OBDResponse r = encode_obd_response(0x0C, 800);
    // 800 * 4 = 3200 = 0x0C80
    CHECK(r.data[3] == 0x0C);
    CHECK(r.data[4] == 0x80);
}

TEST(obd_rpm_zero)
{
    printf("  obd_rpm zero...\n");
    OBDResponse r = encode_obd_response(0x0C, 0);
    CHECK(r.data[3] == 0x00);
    CHECK(r.data[4] == 0x00);
}

TEST(obd_temp)
{
    printf("  obd_temp encoding (90°C)...\n");
    OBDResponse r = encode_obd_response(0x05, 90);
    CHECK(r.respond == true);
    CHECK(r.data[0] == 0x03);
    CHECK(r.data[1] == 0x41);
    CHECK(r.data[2] == 0x05);
    CHECK(r.data[3] == 130); // 90 + 40 = 130
}

TEST(obd_temp_negative)
{
    printf("  obd_temp negative (-40°C = minimum)...\n");
    OBDResponse r = encode_obd_response(0x05, -40);
    CHECK(r.data[3] == 0); // -40 + 40 = 0
}

TEST(obd_temp_zero)
{
    printf("  obd_temp zero (0°C)...\n");
    OBDResponse r = encode_obd_response(0x05, 0);
    CHECK(r.data[3] == 40); // 0 + 40 = 40
}

TEST(obd_load)
{
    printf("  obd_load encoding (50%%)...\n");
    OBDResponse r = encode_obd_response(0x04, 50);
    CHECK(r.respond == true);
    CHECK(r.data[0] == 0x03);
    CHECK(r.data[3] == 127); // 50 * 255 / 100 = 127
}

TEST(obd_load_full)
{
    printf("  obd_load 100%%...\n");
    OBDResponse r = encode_obd_response(0x04, 100);
    CHECK(r.data[3] == 255); // 100 * 255 / 100 = 255
}

TEST(obd_load_zero)
{
    printf("  obd_load 0%%...\n");
    OBDResponse r = encode_obd_response(0x04, 0);
    CHECK(r.data[3] == 0);
}

TEST(obd_fuel)
{
    printf("  obd_fuel encoding (75%%)...\n");
    OBDResponse r = encode_obd_response(0x2F, 75);
    CHECK(r.respond == true);
    CHECK(r.data[0] == 0x03);
    CHECK(r.data[2] == 0x2F);
    CHECK(r.data[3] == 191); // 75 * 255 / 100 = 191
}

TEST(obd_battery)
{
    printf("  obd_battery encoding (12600 mV)...\n");
    OBDResponse r = encode_obd_response(0x42, 12600);
    CHECK(r.respond == true);
    CHECK(r.data[0] == 0x04);
    CHECK(r.data[2] == 0x42);
    // 12600 = 0x3138
    CHECK(r.data[3] == 0x31);
    CHECK(r.data[4] == 0x38);
}

TEST(obd_intake)
{
    printf("  obd_intake encoding (101 kPa)...\n");
    OBDResponse r = encode_obd_response(0x0B, 101);
    CHECK(r.respond == true);
    CHECK(r.data[0] == 0x03);
    CHECK(r.data[3] == 101);
}

TEST(obd_maf)
{
    printf("  obd_maf encoding (0x0540)...\n");
    OBDResponse r = encode_obd_response(0x10, 0x0540);
    CHECK(r.respond == true);
    CHECK(r.data[0] == 0x04);
    CHECK(r.data[3] == 0x05);
    CHECK(r.data[4] == 0x40);
}

TEST(obd_unsupported_pid)
{
    printf("  obd unsupported PID (0xFF)...\n");
    OBDResponse r = encode_obd_response(0xFF, 0);
    CHECK(r.respond == false);
}

TEST(obd_unsupported_pid_0x01)
{
    printf("  obd unsupported PID (0x01)...\n");
    OBDResponse r = encode_obd_response(0x01, 0);
    CHECK(r.respond == false);
}

// Supported PIDs bitmask tests

TEST(obd_supported_pids_00)
{
    printf("  obd supported PIDs 01-20 (PID 0x00)...\n");
    OBDResponse r = encode_obd_response(0x00, 0);
    CHECK(r.respond == true);
    CHECK(r.data[0] == 0x06);
    // Bitmask: PIDs 04,05,0B,0C,0D,10 + bit for 0x20
    CHECK(r.data[3] == 0x18); // bits for 04,05
    CHECK(r.data[4] == 0x39); // bits for 0B,0C,0D
    CHECK(r.data[5] == 0x00);
    CHECK(r.data[6] == 0x01); // bit for 10 + support for 0x20
}

TEST(obd_supported_pids_20)
{
    printf("  obd supported PIDs 21-40 (PID 0x20)...\n");
    OBDResponse r = encode_obd_response(0x20, 0);
    CHECK(r.respond == true);
    CHECK(r.data[3] == 0x00);
    CHECK(r.data[4] == 0x02); // bit for 2F
    CHECK(r.data[6] == 0x01); // support for 0x40
}

TEST(obd_supported_pids_40)
{
    printf("  obd supported PIDs 41-60 (PID 0x40)...\n");
    OBDResponse r = encode_obd_response(0x40, 0);
    CHECK(r.respond == true);
    CHECK(r.data[3] == 0x40); // bit for 42
}

// ============================================================
// parse_int Tests
// ============================================================

TEST(parse_int_valid)
{
    printf("  parse_int valid...\n");
    long out;
    CHECK(parse_int("123", out, 0, 255) == true);
    CHECK(out == 123);
}

TEST(parse_int_negative)
{
    printf("  parse_int negative...\n");
    long out;
    CHECK(parse_int("-40", out, -40, 215) == true);
    CHECK(out == -40);
}

TEST(parse_int_boundary_min)
{
    printf("  parse_int at min boundary...\n");
    long out;
    CHECK(parse_int("0", out, 0, 255) == true);
    CHECK(out == 0);
}

TEST(parse_int_boundary_max)
{
    printf("  parse_int at max boundary...\n");
    long out;
    CHECK(parse_int("255", out, 0, 255) == true);
    CHECK(out == 255);
}

TEST(parse_int_out_of_range_high)
{
    printf("  parse_int out of range (high)...\n");
    long out;
    CHECK(parse_int("256", out, 0, 255) == false);
}

TEST(parse_int_out_of_range_low)
{
    printf("  parse_int out of range (low)...\n");
    long out;
    CHECK(parse_int("-41", out, -40, 215) == false);
}

TEST(parse_int_empty)
{
    printf("  parse_int empty string...\n");
    long out;
    CHECK(parse_int("", out, 0, 255) == false);
}

TEST(parse_int_non_numeric)
{
    printf("  parse_int non-numeric...\n");
    long out;
    CHECK(parse_int("abc", out, 0, 255) == false);
}

TEST(parse_int_trailing_junk)
{
    printf("  parse_int trailing junk...\n");
    long out;
    CHECK(parse_int("123abc", out, 0, 255) == false);
}

TEST(parse_int_whitespace)
{
    printf("  parse_int leading whitespace...\n");
    long out;
    // strtol skips leading whitespace, but our end-pointer check will pass
    // since strtol consumes it. This is acceptable behavior.
    CHECK(parse_int(" 42", out, 0, 255) == true);
    CHECK(out == 42);
}

// ============================================================
// safe_stoi Tests
// ============================================================

TEST(safe_stoi_valid)
{
    printf("  safe_stoi valid...\n");
    CHECK(safe_stoi("8080", 9999) == 8080);
}

TEST(safe_stoi_trailing_junk)
{
    printf("  safe_stoi trailing junk...\n");
    CHECK(safe_stoi("8080junk", 9999) == 9999);
}

TEST(safe_stoi_empty)
{
    printf("  safe_stoi empty...\n");
    CHECK(safe_stoi("", 9999) == 9999);
}

TEST(safe_stoi_non_numeric)
{
    printf("  safe_stoi non-numeric...\n");
    CHECK(safe_stoi("abc", 42) == 42);
}

TEST(safe_stoi_range_valid)
{
    printf("  safe_stoi with range check...\n");
    CHECK(safe_stoi("8080", 9999, 1, 65535) == 8080);
}

TEST(safe_stoi_range_too_high)
{
    printf("  safe_stoi port too high...\n");
    CHECK(safe_stoi("70000", 8080, 1, 65535) == 8080);
}

TEST(safe_stoi_range_too_low)
{
    printf("  safe_stoi port too low...\n");
    CHECK(safe_stoi("0", 8080, 1, 65535) == 8080);
}

TEST(safe_stoi_negative)
{
    printf("  safe_stoi negative port...\n");
    CHECK(safe_stoi("-1", 8080, 1, 65535) == 8080);
}

// ============================================================
// safe_stof Tests
// ============================================================

TEST(safe_stof_valid)
{
    printf("  safe_stof valid...\n");
    float v = safe_stof("2.5", 1.0f);
    CHECK(v > 2.4f && v < 2.6f);
}

TEST(safe_stof_trailing_junk)
{
    printf("  safe_stof trailing junk...\n");
    CHECK(safe_stof("1.0x", 5.0f) == 5.0f);
}

TEST(safe_stof_empty)
{
    printf("  safe_stof empty...\n");
    CHECK(safe_stof("", 1.0f) == 1.0f);
}

TEST(safe_stof_too_small)
{
    printf("  safe_stof too small (0.001)...\n");
    CHECK(safe_stof("0.001", 1.0f) == 1.0f);
}

TEST(safe_stof_too_large)
{
    printf("  safe_stof too large (200)...\n");
    CHECK(safe_stof("200", 1.0f) == 1.0f);
}

TEST(safe_stof_boundary_low)
{
    printf("  safe_stof boundary low (0.01)...\n");
    float v = safe_stof("0.01", 1.0f);
    CHECK(v > 0.009f && v < 0.011f);
}

TEST(safe_stof_boundary_high)
{
    printf("  safe_stof boundary high (100)...\n");
    float v = safe_stof("100", 1.0f);
    CHECK(v > 99.9f && v < 100.1f);
}

// ============================================================
// Config File Parsing Tests
// ============================================================

TEST(config_basic)
{
    printf("  config basic parsing...\n");
    std::string path = write_temp_config("KEY=value\nFOO=bar\n");
    auto cfg = read_config(path);
    CHECK(cfg.size() == 2);
    CHECK(cfg["KEY"] == "value");
    CHECK(cfg["FOO"] == "bar");
    unlink(path.c_str());
}

TEST(config_whitespace_around_equals)
{
    printf("  config whitespace around '='...\n");
    std::string path = write_temp_config("CAN_NODE = can0\nTCP_PORT = 9090\n");
    auto cfg = read_config(path);
    CHECK(cfg["CAN_NODE"] == "can0");
    CHECK(cfg["TCP_PORT"] == "9090");
    unlink(path.c_str());
}

TEST(config_comments_and_blanks)
{
    printf("  config comments and blank lines...\n");
    std::string path = write_temp_config("# comment\n\nKEY=val\n  # indented comment\n");
    auto cfg = read_config(path);
    CHECK(cfg.size() == 1);
    CHECK(cfg["KEY"] == "val");
    unlink(path.c_str());
}

TEST(config_leading_whitespace)
{
    printf("  config leading whitespace on line...\n");
    std::string path = write_temp_config("  KEY=val\n");
    auto cfg = read_config(path);
    CHECK(cfg["KEY"] == "val");
    unlink(path.c_str());
}

TEST(config_trailing_whitespace)
{
    printf("  config trailing whitespace on value...\n");
    std::string path = write_temp_config("KEY=val   \n");
    auto cfg = read_config(path);
    CHECK(cfg["KEY"] == "val");
    unlink(path.c_str());
}

TEST(config_empty_value)
{
    printf("  config empty value...\n");
    std::string path = write_temp_config("KEY=\n");
    auto cfg = read_config(path);
    CHECK(cfg.count("KEY") == 1);
    CHECK(cfg["KEY"].empty());
    unlink(path.c_str());
}

TEST(config_nonexistent_file)
{
    printf("  config nonexistent file...\n");
    auto cfg = read_config("/tmp/does-not-exist-12345.conf");
    CHECK(cfg.empty());
}

TEST(config_boolean_case)
{
    printf("  config boolean values with mixed case...\n");
    std::string path = write_temp_config("A=True\nB=FALSE\nC=true\n");
    auto cfg = read_config(path);
    CHECK(cfg["A"] == "True");
    CHECK(cfg["B"] == "FALSE");
    CHECK(cfg["C"] == "true");
    unlink(path.c_str());
}

TEST(config_equals_in_value)
{
    printf("  config value containing '='...\n");
    std::string path = write_temp_config("KEY=a=b\n");
    auto cfg = read_config(path);
    CHECK(cfg["KEY"] == "a=b");
    unlink(path.c_str());
}

// ============================================================

int main()
{
    printf("\ncar-can-emulator test suite\n");
    printf("==========================\n\n");

    // Tests are auto-registered and already ran via static constructors above.
    // Just report results.

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    if (tests_passed != tests_run) {
        printf("SOME TESTS FAILED!\n");
        return 1;
    }
    printf("ALL TESTS PASSED.\n");
    return 0;
}
