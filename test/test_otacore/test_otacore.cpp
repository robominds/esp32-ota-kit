// Host tests for lib/otacore: version parsing, ordering, and manifest parsing.
//
// Author: Mark Castelluccio <markacastelluccio@gmail.com>
// Written with assistance from Claude Code (Anthropic Claude Opus 5).

#include <unity.h>

#include <cstring>
#include <string>

#include "otacore.h"

using otacore::Manifest;
using otacore::Version;

void setUp(void) {}
void tearDown(void) {}

// --- parseVersion ----------------------------------------------------------

static void test_parse_simple(void) {
    Version v;
    TEST_ASSERT_TRUE(otacore::parseVersion("1.2.3", v));
    TEST_ASSERT_EQUAL_INT(1, v.major);
    TEST_ASSERT_EQUAL_INT(2, v.minor);
    TEST_ASSERT_EQUAL_INT(3, v.patch);
}

static void test_parse_multi_digit(void) {
    Version v;
    TEST_ASSERT_TRUE(otacore::parseVersion("10.20.300", v));
    TEST_ASSERT_EQUAL_INT(10, v.major);
    TEST_ASSERT_EQUAL_INT(20, v.minor);
    TEST_ASSERT_EQUAL_INT(300, v.patch);
}

static void test_parse_zero(void) {
    Version v;
    TEST_ASSERT_TRUE(otacore::parseVersion("0.0.0", v));
}

static void test_parse_rejects_missing_part(void) {
    Version v;
    TEST_ASSERT_FALSE(otacore::parseVersion("1.2", v));
    TEST_ASSERT_FALSE(otacore::parseVersion("1", v));
    TEST_ASSERT_FALSE(otacore::parseVersion("", v));
}

static void test_parse_rejects_extra_part(void) {
    Version v;
    TEST_ASSERT_FALSE(otacore::parseVersion("1.2.3.4", v));
}

static void test_parse_rejects_prefix_suffix(void) {
    Version v;
    TEST_ASSERT_FALSE(otacore::parseVersion("v1.2.3", v));
    TEST_ASSERT_FALSE(otacore::parseVersion("1.2.3-beta", v));
    TEST_ASSERT_FALSE(otacore::parseVersion(" 1.2.3", v));
    TEST_ASSERT_FALSE(otacore::parseVersion("1.2.3 ", v));
}

static void test_parse_rejects_leading_zero(void) {
    Version v;
    TEST_ASSERT_FALSE(otacore::parseVersion("01.2.3", v));
    TEST_ASSERT_FALSE(otacore::parseVersion("1.02.3", v));
}

static void test_parse_rejects_non_digit(void) {
    Version v;
    TEST_ASSERT_FALSE(otacore::parseVersion("1.x.3", v));
    TEST_ASSERT_FALSE(otacore::parseVersion("1..3", v));
    TEST_ASSERT_FALSE(otacore::parseVersion("-1.2.3", v));
}

static void test_parse_null_is_false(void) {
    Version v;
    TEST_ASSERT_FALSE(otacore::parseVersion(nullptr, v));
}

// --- compareVersions -------------------------------------------------------

static Version mk(int a, int b, int c) { Version v; v.major = a; v.minor = b; v.patch = c; return v; }

static void test_compare_equal(void) {
    TEST_ASSERT_EQUAL_INT(0, otacore::compareVersions(mk(1, 2, 3), mk(1, 2, 3)));
}

static void test_compare_major_dominates(void) {
    TEST_ASSERT_EQUAL_INT(-1, otacore::compareVersions(mk(1, 9, 9), mk(2, 0, 0)));
    TEST_ASSERT_EQUAL_INT(1, otacore::compareVersions(mk(2, 0, 0), mk(1, 9, 9)));
}

static void test_compare_minor_then_patch(void) {
    TEST_ASSERT_EQUAL_INT(-1, otacore::compareVersions(mk(1, 1, 9), mk(1, 2, 0)));
    TEST_ASSERT_EQUAL_INT(-1, otacore::compareVersions(mk(1, 2, 3), mk(1, 2, 4)));
    TEST_ASSERT_EQUAL_INT(1, otacore::compareVersions(mk(1, 2, 10), mk(1, 2, 9)));
}

// --- parseManifest ---------------------------------------------------------

static const char* kGood =
    "{\"version\":\"1.0.1\",\"url\":\"http://192.168.1.10:8000/firmware.bin\","
    "\"size\":1402368,\"md5\":\"0123456789abcdef0123456789abcdef\"}";

static void test_manifest_good(void) {
    Manifest m; std::string err;
    TEST_ASSERT_TRUE(otacore::parseManifest(kGood, strlen(kGood), m, err));
    TEST_ASSERT_EQUAL_STRING("1.0.1", m.version.c_str());
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.10:8000/firmware.bin", m.url.c_str());
    TEST_ASSERT_EQUAL_size_t(1402368, m.size);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", m.md5.c_str());
    TEST_ASSERT_TRUE(err.empty());
}

static void test_manifest_extra_fields_ignored(void) {
    const char* j = "{\"version\":\"1.0.1\",\"url\":\"http://h/f.bin\",\"size\":10,"
                    "\"md5\":\"0123456789abcdef0123456789abcdef\",\"notes\":\"x\"}";
    Manifest m; std::string err;
    TEST_ASSERT_TRUE(otacore::parseManifest(j, strlen(j), m, err));
}

static void test_manifest_missing_field(void) {
    const char* j = "{\"version\":\"1.0.1\",\"url\":\"http://h/f.bin\",\"size\":10}";
    Manifest m; std::string err;
    TEST_ASSERT_FALSE(otacore::parseManifest(j, strlen(j), m, err));
    TEST_ASSERT_EQUAL_STRING("manifest: missing md5", err.c_str());
}

static void test_manifest_bad_version(void) {
    const char* j = "{\"version\":\"1.0\",\"url\":\"http://h/f.bin\",\"size\":10,"
                    "\"md5\":\"0123456789abcdef0123456789abcdef\"}";
    Manifest m; std::string err;
    TEST_ASSERT_FALSE(otacore::parseManifest(j, strlen(j), m, err));
    TEST_ASSERT_EQUAL_STRING("manifest: bad version", err.c_str());
}

static void test_manifest_bad_md5_length(void) {
    const char* j = "{\"version\":\"1.0.1\",\"url\":\"http://h/f.bin\",\"size\":10,"
                    "\"md5\":\"abc\"}";
    Manifest m; std::string err;
    TEST_ASSERT_FALSE(otacore::parseManifest(j, strlen(j), m, err));
    TEST_ASSERT_EQUAL_STRING("manifest: bad md5", err.c_str());
}

static void test_manifest_zero_size(void) {
    const char* j = "{\"version\":\"1.0.1\",\"url\":\"http://h/f.bin\",\"size\":0,"
                    "\"md5\":\"0123456789abcdef0123456789abcdef\"}";
    Manifest m; std::string err;
    TEST_ASSERT_FALSE(otacore::parseManifest(j, strlen(j), m, err));
    TEST_ASSERT_EQUAL_STRING("manifest: bad size", err.c_str());
}

static void test_manifest_size_wrong_type(void) {
    const char* j = "{\"version\":\"1.0.1\",\"url\":\"http://h/f.bin\",\"size\":\"10\","
                    "\"md5\":\"0123456789abcdef0123456789abcdef\"}";
    Manifest m; std::string err;
    TEST_ASSERT_FALSE(otacore::parseManifest(j, strlen(j), m, err));
    TEST_ASSERT_EQUAL_STRING("manifest: bad size", err.c_str());
}

static void test_manifest_not_json(void) {
    const char* j = "<html>nope</html>";
    Manifest m; std::string err;
    TEST_ASSERT_FALSE(otacore::parseManifest(j, strlen(j), m, err));
    TEST_ASSERT_EQUAL_STRING("manifest: not json", err.c_str());
}

static void test_manifest_empty(void) {
    Manifest m; std::string err;
    TEST_ASSERT_FALSE(otacore::parseManifest("", 0, m, err));
    TEST_ASSERT_EQUAL_STRING("manifest: not json", err.c_str());
}

static void test_manifest_md5_too_long(void) {
    const char* j = "{\"version\":\"1.0.1\",\"url\":\"http://h/f.bin\",\"size\":10,"
                    "\"md5\":\"0123456789abcdef0123456789abcdef0\"}";
    Manifest m; std::string err;
    TEST_ASSERT_FALSE(otacore::parseManifest(j, strlen(j), m, err));
    TEST_ASSERT_EQUAL_STRING("manifest: bad md5", err.c_str());
}

static void test_manifest_md5_not_hex(void) {
    const char* j = "{\"version\":\"1.0.1\",\"url\":\"http://h/f.bin\",\"size\":10,"
                    "\"md5\":\"0123456789abcdef0123456789abcdeg\"}";
    Manifest m; std::string err;
    TEST_ASSERT_FALSE(otacore::parseManifest(j, strlen(j), m, err));
    TEST_ASSERT_EQUAL_STRING("manifest: bad md5", err.c_str());
}

static void test_manifest_md5_uppercase_is_lowercased(void) {
    const char* j = "{\"version\":\"1.0.1\",\"url\":\"http://h/f.bin\",\"size\":10,"
                    "\"md5\":\"0123456789ABCDEF0123456789ABCDEF\"}";
    Manifest m; std::string err;
    TEST_ASSERT_TRUE(otacore::parseManifest(j, strlen(j), m, err));
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", m.md5.c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_simple);
    RUN_TEST(test_parse_multi_digit);
    RUN_TEST(test_parse_zero);
    RUN_TEST(test_parse_rejects_missing_part);
    RUN_TEST(test_parse_rejects_extra_part);
    RUN_TEST(test_parse_rejects_prefix_suffix);
    RUN_TEST(test_parse_rejects_leading_zero);
    RUN_TEST(test_parse_rejects_non_digit);
    RUN_TEST(test_parse_null_is_false);
    RUN_TEST(test_compare_equal);
    RUN_TEST(test_compare_major_dominates);
    RUN_TEST(test_compare_minor_then_patch);
    RUN_TEST(test_manifest_good);
    RUN_TEST(test_manifest_extra_fields_ignored);
    RUN_TEST(test_manifest_missing_field);
    RUN_TEST(test_manifest_bad_version);
    RUN_TEST(test_manifest_bad_md5_length);
    RUN_TEST(test_manifest_zero_size);
    RUN_TEST(test_manifest_size_wrong_type);
    RUN_TEST(test_manifest_not_json);
    RUN_TEST(test_manifest_empty);
    RUN_TEST(test_manifest_md5_too_long);
    RUN_TEST(test_manifest_md5_not_hex);
    RUN_TEST(test_manifest_md5_uppercase_is_lowercased);
    return UNITY_END();
}
