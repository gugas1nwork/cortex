#include "chat_cmd.h"
#include "httplib.h"

#include "trantor/utils/Logger.h"

namespace commands {
namespace {
constexpr const char* kExitChat = "exit()";
constexpr const auto kMinDataChunkSize = 6u;
constexpr const char* kUser = "user";
constexpr const char* kAssistant = "assistant";

}  // namespace

struct ChunkParser {
  std::string content;
  bool is_done = false;

  ChunkParser(const char* data, size_t data_length) {
    if (data && data_length > kMinDataChunkSize) {
      std::string s(data + kMinDataChunkSize, data_length - kMinDataChunkSize);
      if (s.find("[DONE]") != std::string::npos) {
        is_done = true;
      } else {
        content = nlohmann::json::parse(s)["choices"][0]["delta"]["content"];
      }
    }
  }
};

ChatCmd::ChatCmd(std::string host, int port, const config::ModelConfig& mc)
    : host_(std::move(host)), port_(port), mc_(mc) {}

void ChatCmd::Exec(std::string msg) {
  auto address = host_ + ":" + std::to_string(port_);
  // Check if model is loaded
  // TODO(sang) only llamacpp support modelstatus for now
  if (mc_.engine.find("llamacpp") != std::string::npos) {
    httplib::Client cli(address);
    nlohmann::json json_data;
    json_data["model"] = mc_.name;
    json_data["engine"] = mc_.engine;

    auto data_str = json_data.dump();

    // TODO: move this to another message?
    auto res = cli.Post("/inferences/server/modelstatus", httplib::Headers(),
                        data_str.data(), data_str.size(), "application/json");
    if (res) {
      if (res->status != httplib::StatusCode::OK_200) {
        LOG_INFO << res->body;
        return;
      }
    } else {
      auto err = res.error();
      LOG_WARN << "HTTP error: " << httplib::to_string(err);
      return;
    }
  }

  // Some instruction for user here
  std::cout << "Inorder to exit, type `exit()`" << std::endl;
  // Model is loaded, start to chat
  {
    while (true) {
      std::string user_input = std::move(msg);
      std::cout << "> ";
      if (user_input.empty()) {
        std::getline(std::cin, user_input);
      }
      if (user_input == kExitChat) {
        break;
      }

      if (!user_input.empty()) {
        httplib::Client cli(address);
        nlohmann::json json_data;
        nlohmann::json new_data;
        new_data["role"] = kUser;
        new_data["content"] = user_input;
        histories_.push_back(std::move(new_data));
        json_data["engine"] = mc_.engine;
        json_data["messages"] = histories_;
        json_data["model"] = mc_.name;
        //TODO: support non-stream
        json_data["stream"] = true;
        json_data["stop"] = mc_.stop;
        auto data_str = json_data.dump();
        // std::cout << data_str << std::endl;
        cli.set_read_timeout(std::chrono::seconds(60));
        // std::cout << "> ";
        httplib::Request req;
        req.headers = httplib::Headers();
        req.set_header("Content-Type", "application/json");
        req.method = "POST";
        req.path = "/v1/chat/completions";
        req.body = data_str;
        std::string ai_chat;
        req.content_receiver = [&](const char* data, size_t data_length,
                                   uint64_t offset, uint64_t total_length) {
          ChunkParser cp(data, data_length);
          if (cp.is_done) {
            std::cout << std::endl;
            return false;
          }
          std::cout << cp.content;
          ai_chat += cp.content;
          return true;
        };
        cli.send(req);

        nlohmann::json ai_res;
        ai_res["role"] = kAssistant;
        ai_res["content"] = ai_chat;
        histories_.push_back(std::move(ai_res));
      }
      // std::cout << "ok Done" << std::endl;
    }
  }
}

};  // namespace commands