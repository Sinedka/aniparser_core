#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>

#include "../tui/ipc_client.hpp"
#include "ipc_server.hpp"
#include "server.hpp"

namespace {
using namespace std::chrono_literals;
using aniparser::tui::IpcClient;
using Json = nlohmann::json;

class IpcServerChecks : public testing::Test {
  protected:
  void SetUp() override {
    const auto root =
        std::filesystem::path("out/ipc-server-checks") /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    endpoint = (root / "server.sock").string();
    ASSERT_EQ(::setenv("ANIPARSER_DATA_DIR", (root / "data").c_str(), 1), 0);
  }

  Core::ApiBackend backend() {
    Core::ApiBackend api;
    api.search = [](const yani::SearchInput &)
        -> yani::Result<std::vector<yani::SearchResult>> { return {}; };
    return api;
  }

  std::string endpoint;
};

Json await_favorites_change(IpcClient &client, const Json &subscription_id) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  do {
    for (const auto &message : client.poll_messages())
      if (message.value("event", "") == "favorites.updated" &&
          message.value("reason", "") == "favorites" &&
          message.at("id") == subscription_id)
        return message;
    std::this_thread::sleep_for(5ms);
  } while (std::chrono::steady_clock::now() < deadline);
  return nullptr;
}

TEST_F(IpcServerChecks, RemainingClientReceivesUpdatesAfterPeerCloses) {
  Core core(backend());
  Server server(core);
  IpcServer ipc(server, endpoint);
  ASSERT_TRUE(ipc.start());

  IpcClient second(2s);
  Json subscription;
  Json added;
  const Json anime{{"anime_id", 42}};
  {
    IpcClient first(2s);
    ASSERT_TRUE(first.connect_to(endpoint));
    EXPECT_EQ(first.request("hello").at("protocol"), 3);
    ASSERT_TRUE(second.connect_to(endpoint));
    EXPECT_EQ(second.request("hello").at("protocol"), 3);
    const auto first_subscription = first.request("subscribeFavorites");
    subscription = second.request("subscribeFavorites");
    ASSERT_EQ(first_subscription.at("ok"), true);
    ASSERT_EQ(subscription.at("ok"), true);
    EXPECT_EQ(first_subscription.at("id"), subscription.at("id"));
    EXPECT_EQ(subscription.at("items"), Json::array());

    ASSERT_EQ(first.request("addFavorite", anime).at("ok"), true);
    added = await_favorites_change(second, subscription.at("id"));
    ASSERT_TRUE(added.is_object());
    ASSERT_EQ(added.at("ok"), true);
    ASSERT_EQ(added.at("items").size(), 1U);
    EXPECT_EQ(added.at("items").at(0).at("anime_id"), 42);
  }

  EXPECT_EQ(second.request("isFavorite", anime).at("value"), true);
  ASSERT_EQ(second.request("removeFavorite", anime).at("removed"), true);
  const auto removed = await_favorites_change(second, subscription.at("id"));
  ASSERT_TRUE(removed.is_object());
  EXPECT_EQ(removed.at("ok"), true);
  EXPECT_EQ(removed.at("items"), Json::array());
  EXPECT_GT(removed.at("sequence").get<int>(), added.at("sequence").get<int>());
}

TEST_F(IpcServerChecks, ReconnectReadsFavoritesAfterRestart) {
  const Json anime{{"anime_id", 42}};
  {
    Core core(backend());
    Server server(core);
    IpcServer ipc(server, endpoint);
    ASSERT_TRUE(ipc.start());
    IpcClient client(2s);
    ASSERT_TRUE(client.connect_to(endpoint));
    ASSERT_EQ(client.request("addFavorite", anime).at("ok"), true);
  }
  {
    Core core(backend());
    Server server(core);
    IpcServer ipc(server, endpoint);
    ASSERT_TRUE(ipc.start());
    IpcClient client(2s);
    ASSERT_TRUE(client.connect_to(endpoint));
    EXPECT_EQ(client.request("hello").at("protocol"), 3);
    EXPECT_EQ(client.request("isFavorite", anime).at("value"), true);
  }
}
} // namespace
