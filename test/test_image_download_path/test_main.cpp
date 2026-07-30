#include <cstddef>
#include <cstring>
#include <initializer_list>

#include <unity.h>

#include "pf_web/image_download_path.hpp"

extern "C" void setUp() {}
extern "C" void tearDown() {}

namespace {

void test_download_uri_decodes_a_safe_basename()
{
    char name[pf_storage::kCatalogNameCapacity]{};
    std::size_t written = 0U;
    TEST_ASSERT_TRUE(pf_web::decode_image_download_uri(
        "/api/v1/images/photo%20one.pfr1/download?raw=1",
        name,
        written));
    TEST_ASSERT_EQUAL_STRING("photo one.pfr1", name);
    TEST_ASSERT_EQUAL_UINT(std::strlen(name), written);
}

void test_download_uri_rejects_traversal_and_bad_encoding()
{
    char name[pf_storage::kCatalogNameCapacity]{};
    std::size_t written = 0U;
    for (const char* const uri : {
             "/api/v1/images/%2Fsecret.pfr1/download",
             "/api/v1/images/..%2Fpfr1/download",
             "/api/v1/images/bad%ZZ.pfr1/download",
             "/api/v1/images/missing/downloaded",
             "/api/v1/images//download",
         }) {
        TEST_ASSERT_FALSE(pf_web::decode_image_download_uri(
            uri,
            name,
            written));
    }
}

void test_download_uri_rejects_invalid_route_prefix()
{
    char name[pf_storage::kCatalogNameCapacity]{};
    std::size_t written = 0U;
    TEST_ASSERT_FALSE(pf_web::decode_image_download_uri(
        "/api/v1/images/demo.pfr1",
        name,
        written));
    TEST_ASSERT_FALSE(pf_web::decode_image_download_uri(
        "/api/v1/config/demo.pfr1/download",
        name,
        written));
}

}  // namespace

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_download_uri_decodes_a_safe_basename);
    RUN_TEST(test_download_uri_rejects_traversal_and_bad_encoding);
    RUN_TEST(test_download_uri_rejects_invalid_route_prefix);
    return UNITY_END();
}
