#include <nexilis/object/game_item.hh>
#include <nexilis/protocol_manager.hh>
#include <nexilis/room_data.hh>

#include <nexilis/server/protocol/nxboost/tcp_server.hh>
#include <nexilis/server/room_storage.hh>
#include <nexilis/server/runtime.hh>
#include <nexilis/server/server_config.hh>

#include <iostream>

int main() {
  nexilis::Log::startConsoleDebugging();

  using namespace nexilis::server;

  ServerConfig server_config;
  server_config.setMode(AuthenticationMode::password_protected);

  // User should input this passphrase to access this server.
  server_config.setPassphrase("password");
  server_config.setRootPassword("root");

  // One room in the beginning
  auto room =
      Room(nexilis::RoomData(0, "Room 1", nexilis::Util::getRandomUint64(),
                             nexilis::RoomData::Context::_3D));
  RoomStorage::add(std::move(room));

  nexilis::ProtocolManager protocolManager;

  // Boost TCP server
  auto boostTCPServer =
      protocolManager.createProtocol<nxboost::TCPServer>(server_config);
  boostTCPServer.start();

  auto condition = [](size_t) { return true; };
  auto f = std::function<bool()>([]() { return true; });

  auto server_runtime = std::thread(
      [&condition, &f]() { nexilis::server::runtime(condition, f, 1); });
  server_runtime.detach();

  // Wait for user input to stop the server
  std::cout << "Chat application server is running. Press Enter to stop..."
            << std::endl;
  std::cin.get();
}
