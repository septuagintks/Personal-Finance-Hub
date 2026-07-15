#pragma once

#include <cstddef>

struct CURL;
using CURLcode = int;
using CURLoption = int;
using CURLINFO = int;

inline constexpr CURLcode CURLE_OK = 0;
inline constexpr CURLcode CURLE_OPERATION_TIMEDOUT = 28;
inline constexpr long CURL_GLOBAL_DEFAULT = 3;
inline constexpr CURLoption CURLOPT_URL = 10002;
inline constexpr CURLoption CURLOPT_HTTPGET = 80;
inline constexpr CURLoption CURLOPT_NOSIGNAL = 99;
inline constexpr CURLoption CURLOPT_TIMEOUT_MS = 155;
inline constexpr CURLoption CURLOPT_CONNECTTIMEOUT_MS = 156;
inline constexpr CURLoption CURLOPT_SSL_VERIFYPEER = 64;
inline constexpr CURLoption CURLOPT_SSL_VERIFYHOST = 81;
inline constexpr CURLoption CURLOPT_PROTOCOLS_STR = 10318;
inline constexpr CURLoption CURLOPT_REDIR_PROTOCOLS_STR = 10319;
inline constexpr CURLoption CURLOPT_FOLLOWLOCATION = 52;
inline constexpr CURLoption CURLOPT_ACCEPT_ENCODING = 10102;
inline constexpr CURLoption CURLOPT_USERAGENT = 10018;
inline constexpr CURLoption CURLOPT_WRITEFUNCTION = 20011;
inline constexpr CURLoption CURLOPT_WRITEDATA = 10001;
inline constexpr CURLINFO CURLINFO_RESPONSE_CODE = 0x200002;

extern "C" {
CURLcode curl_global_init(long);
CURL* curl_easy_init();
void curl_easy_cleanup(CURL*);
char* curl_easy_escape(CURL*, const char*, int);
void curl_free(void*);
CURLcode curl_easy_setopt(CURL*, CURLoption, ...);
CURLcode curl_easy_perform(CURL*);
CURLcode curl_easy_getinfo(CURL*, CURLINFO, ...);
}
