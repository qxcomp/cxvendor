/* curl_dyn 测试：只链 -ldl，不链 libcurl */
#include "test_common.h"

typedef void CURL;
typedef int CURLcode;

#define CURLE_OK 0
#define CURLOPT_URL 10002
#define CURLOPT_TIMEOUT 13
#define CURLOPT_WRITEFUNCTION 20011
#define CURLOPT_WRITEDATA 10001

int curl_dyn_init(const char *library_path);
void curl_dyn_cleanup(void);
int curl_libok(void);

CURL *curl_easy_init(void);
void curl_easy_cleanup(CURL *handle);
CURLcode curl_easy_setopt(CURL *handle, int option, ...);
CURLcode curl_easy_perform(CURL *handle);
const char *curl_easy_strerror(CURLcode code);

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *ud) {
    size_t total = size * nmemb;
    size_t *n = ud;
    (void)ptr;
    *n += total;
    return total;
}

int main(int argc, char **argv) {
    int failures = 0;
    size_t got = 0;
    CURL *c;

    if (curl_dyn_init(argc > 1 ? argv[1] : NULL) != 0) {
        fprintf(stderr, "curl_dyn_init failed\n");
        return 1;
    }
    failures += CHECK(curl_libok() != 0);

    c = curl_easy_init();
    failures += CHECK(c != NULL);
    if (!c) return 1;

    failures += CHECK(curl_easy_setopt(c, CURLOPT_URL,
                                       "file:///etc/hostname") == CURLE_OK);
    failures += CHECK(curl_easy_setopt(c, CURLOPT_TIMEOUT, 5) == CURLE_OK);
    failures += CHECK(curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,
                                       write_cb) == CURLE_OK);
    failures += CHECK(curl_easy_setopt(c, CURLOPT_WRITEDATA, &got)
                      == CURLE_OK);

    failures += CHECK(curl_easy_perform(c) == CURLE_OK);
    printf("file bytes=%zu strerror(0)=%s\n", got, curl_easy_strerror(0));

    /* 错误路径：未知选项不崩溃 */
    curl_easy_setopt(c, 999999, 0);
    failures += CHECK(1);

    curl_easy_cleanup(c);
    curl_dyn_cleanup();

    if (failures) return 1;
    printf("all checks passed\n");
    return 0;
}
