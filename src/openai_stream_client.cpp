#include "openai_stream_client.hpp"

#include <curl/curl.h>

namespace {

struct StreamCtx {
  std::string line_buffer;
  std::function<void(const nlohmann::json&)> on_chunk;
  std::function<bool()> should_cancel;
};

std::string RStripCR(std::string s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
  return s;
}

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const size_t total = size * nmemb;
  auto* ctx = static_cast<StreamCtx*>(userdata);
  ctx->line_buffer.append(ptr, total);

  size_t pos = 0;
  while (true) {
    size_t nl = ctx->line_buffer.find('\n', pos);
    if (nl == std::string::npos) {
      ctx->line_buffer.erase(0, pos);
      break;
    }

    std::string line = RStripCR(ctx->line_buffer.substr(pos, nl - pos));
    pos = nl + 1;

    const std::string prefix = "data: ";
    if (line.rfind(prefix, 0) != 0) continue;

    std::string payload = line.substr(prefix.size());
    if (payload == "[DONE]") continue;

    try {
      auto chunk = nlohmann::json::parse(payload);
      ctx->on_chunk(chunk);
    } catch (...) {}
  }

  return total;
}

int ProgressCallback(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  auto* ctx = static_cast<StreamCtx*>(userdata);
  if (!ctx || !ctx->should_cancel) return 0;
  return ctx->should_cancel() ? 1 : 0;
}

}  // namespace

OpenAIStreamClient::OpenAIStreamClient(std::string api_key, std::string base_url)
    : api_key_(std::move(api_key)), base_url_(std::move(base_url)) {}

bool OpenAIStreamClient::StreamChatCompletions(
    const nlohmann::json& request_body,
    const std::function<void(const nlohmann::json&)>& on_chunk,
    const std::function<void(const std::string&)>& on_error,
    const std::function<bool()>& should_cancel) const {
  CURL* curl = curl_easy_init();
  if (!curl) {
    on_error("Failed to initialize CURL");
    return false;
  }

  std::string url = base_url_;
  if (!url.empty() && url.back() == '/') url.pop_back();
  url += "/chat/completions";

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());

  StreamCtx ctx;
  ctx.on_chunk = on_chunk;
  ctx.should_cancel = should_cancel;
  const std::string body = request_body.dump();

  curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
  curl_easy_setopt(curl, CURLOPT_POST,          1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &ctx);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT,       0L);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS,    0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA,  &ctx);

  const CURLcode code = curl_easy_perform(curl);

  long http_status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK) {
    if (code == CURLE_ABORTED_BY_CALLBACK) {
      on_error("API request cancelled");
      return false;
    }
    on_error(std::string("API request failed: ") + curl_easy_strerror(code));
    return false;
  }

  if (http_status >= 400) {
    on_error("API request failed: HTTP " + std::to_string(http_status));
    return false;
  }

  return true;
}
