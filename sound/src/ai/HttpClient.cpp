#include "ai/HttpClient.hpp"

#ifdef HECQUIN_WITH_CURL
#include <curl/curl.h>

#include <iostream>
#include <memory>
#include <mutex>

namespace {

size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct CurlEasyDeleter {
    void operator()(CURL* c) const noexcept { curl_easy_cleanup(c); }
};

struct CurlSlistDeleter {
    void operator()(curl_slist* s) const noexcept { curl_slist_free_all(s); }
};

using CurlEasyPtr = std::unique_ptr<CURL, CurlEasyDeleter>;
using CurlSlistPtr = std::unique_ptr<curl_slist, CurlSlistDeleter>;

} // namespace

std::optional<HttpResult> http_post_json(const std::string& url,
                                         const std::string& bearer_token,
                                         const std::string& json_body,
                                         long timeout_seconds) {
    static std::once_flag curl_init_once;
    std::call_once(curl_init_once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CurlEasyPtr curl(curl_easy_init());
    if (!curl) {
        return std::nullopt;
    }

    curl_slist* raw_headers = nullptr;
    raw_headers = curl_slist_append(raw_headers, "Content-Type: application/json");
    const std::string auth = "Authorization: Bearer " + bearer_token;
    raw_headers = curl_slist_append(raw_headers, auth.c_str());
    CurlSlistPtr headers(raw_headers);

    std::string response;
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, json_body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeout_seconds);
    // Give callers a human-readable reason for transport failures.
    curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, errbuf);
    // Fail fast on stalled connections (no bytes in/out for 20s).
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 20L);
    // Reasonable connect budget; keeps the app responsive on bad DNS.
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 15L);

    const CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        std::cerr << "[http] curl error " << static_cast<int>(res) << ": "
                  << curl_easy_strerror(res);
        if (errbuf[0] != '\0') {
            std::cerr << " (" << errbuf << ")";
        }
        std::cerr << " url=" << url << std::endl;
        return std::nullopt;
    }

    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
    return HttpResult{http_code, std::move(response)};
}

#else

std::optional<HttpResult> http_post_json(const std::string& /*url*/,
                                         const std::string& /*bearer_token*/,
                                         const std::string& /*json_body*/,
                                         long /*timeout_seconds*/) {
    return std::nullopt;
}

#endif
