#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nexilis/client/packet.hh>
#include <nexilis/protocol_manager.hh>
#include <nexilis/start_client.hh>
#include <nexilis/tcp_client.hh>

#include <ncurses.h>

class ChatClient {
public:
  ChatClient();
  ~ChatClient();

  void run();

private:
  void setupUI();
  void collectInputFields();
  std::string readField(const std::string &label,
                        const std::string &default_value, bool hidden);
  void handleInput();
  void updateUI();
  void connectToServer(const std::vector<nexilis::RoomInfo> &rooms);
  void sendMessage(const std::string &message);
  void pollRoomMessages();
  void displayMessages();
  void displayInputFields();
  void displayStatus();

  void updateMessages(const std::string &message);

  nexilis::ProtocolManager m_protocol_manager;
  std::unique_ptr<nexilis::TCPClient> m_tcp_client;

  // UI elements
  WINDOW *input_win;
  WINDOW *message_win;
  WINDOW *status_win;

  // Input fields
  std::string server_ip;
  std::string server_password;
  std::string client_username;

  // Message currently being typed
  std::string current_message;

  // Connection state
  bool connected;
  bool authenticated;

  // Messages
  std::vector<std::string> messages;

  // Joined room state
  uint64_t m_room_id = 0;
  size_t m_last_message_count = 0;
};

ChatClient::ChatClient() : connected(false), authenticated(false) {
  // Initialize ncurses
  initscr();
  cbreak();
  noecho();
  curs_set(1);
  keypad(stdscr, TRUE);

  // Create windows
  setupUI();
}

ChatClient::~ChatClient() { endwin(); }

void ChatClient::setupUI() {
  int height, width;
  getmaxyx(stdscr, height, width);

  // Create windows
  message_win = newwin(height - 10, width, 0, 0);
  input_win = newwin(7, width, height - 7, 0);
  status_win = newwin(3, width, height - 10, 0);

  keypad(input_win, TRUE);
  nodelay(input_win, TRUE);

  box(message_win, 0, 0);
  box(input_win, 0, 0);
  box(status_win, 0, 0);

  refresh();
  wrefresh(message_win);
  wrefresh(input_win);
  wrefresh(status_win);
}

std::string ChatClient::readField(const std::string &label,
                                  const std::string &default_value,
                                  bool hidden) {
  std::string value;

  while (true) {
    werase(input_win);
    box(input_win, 0, 0);

    std::string shown = value;
    if (hidden && !shown.empty()) {
      shown.assign(shown.size(), '*');
    }

    mvwprintw(input_win, 1, 1, "Enter %s:", label.c_str());
    mvwprintw(input_win, 2, 1, "  [ %s ]",
              value.empty() ? default_value.c_str() : shown.c_str());
    mvwprintw(input_win, 4, 1,
              "Type a value, or leave empty to use the default. Enter = done");
    wrefresh(input_win);

    int ch = wgetch(input_win);
    if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
      return value.empty() ? default_value : value;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8 || ch == KEY_DC) {
      if (!value.empty()) {
        value.pop_back();
      }
    } else if (ch >= 32 && ch <= 126) {
      if (value.size() < 64) {
        value.push_back(static_cast<char>(ch));
      }
    }
  }
}

void ChatClient::collectInputFields() {
  nodelay(input_win, FALSE);

  server_ip = readField("server IP address", "127.0.0.1", false);
  server_password = readField("server password", "password", true);
  client_username = readField("username", "user", false);

  nodelay(input_win, TRUE);
}

void ChatClient::run() {
  collectInputFields();

  m_tcp_client = std::make_unique<nexilis::TCPClient>(
      &m_protocol_manager, server_ip, server_password);

  std::vector<nexilis::RoomInfo> rooms;
  std::atomic<bool> ready = false;
  std::mutex mtx;
  auto start_thread = nexilis::startClient(*m_tcp_client, rooms, ready, mtx);
  start_thread.detach();

  // Wait for the server connection and room discovery (max ~10s).
  for (int i = 0; i < 200 && !ready.load(); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::vector<nexilis::RoomInfo> discovered_rooms;
  {
    std::lock_guard<std::mutex> lock(mtx);
    discovered_rooms = rooms;
  }

  connectToServer(discovered_rooms);
  connected = true;

  // Main chat loop
  while (true) {
    handleInput();
    updateUI();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

void ChatClient::handleInput() {
  int ch = wgetch(input_win);
  if (ch == ERR) {
    return;
  }

  if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
    if (!current_message.empty()) {
      sendMessage(current_message);
      current_message.clear();
    }
    return;
  }
  if (ch == KEY_BACKSPACE || ch == 127 || ch == 8 || ch == KEY_DC) {
    if (!current_message.empty()) {
      current_message.pop_back();
    }
    return;
  }
  if (ch == 'q' || ch == 'Q') {
    endwin();
    exit(0);
  }
  if (ch >= 32 && ch <= 126) {
    if (current_message.size() < 256) {
      current_message.push_back(static_cast<char>(ch));
    }
  }
}

void ChatClient::updateUI() {
  pollRoomMessages();
  displayMessages();
  displayInputFields();
  displayStatus();

  wrefresh(message_win);
  wrefresh(input_win);
  wrefresh(status_win);
}

void ChatClient::displayMessages() {
  werase(message_win);
  box(message_win, 0, 0);

  int win_height, win_width;
  getmaxyx(message_win, win_height, win_width);

  int max_rows = win_height - 2;
  int start = messages.size() > static_cast<size_t>(max_rows)
                  ? static_cast<int>(messages.size()) - max_rows
                  : 0;

  int row = 1;
  for (size_t i = start; i < messages.size(); ++i) {
    mvwprintw(message_win, row++, 1, "%s", messages[i].c_str());
  }
}

void ChatClient::displayInputFields() {
  werase(input_win);
  box(input_win, 0, 0);

  mvwprintw(input_win, 1, 1, "Server: %s", server_ip.c_str());
  mvwprintw(input_win, 2, 1, "User:   %s", client_username.c_str());
  mvwprintw(input_win, 3, 1, "> %s", current_message.c_str());
  mvwprintw(input_win, 5, 1, "Enter: send    q: quit");

  wmove(input_win, 3, 3 + static_cast<int>(current_message.size()));
  wrefresh(input_win);
}

void ChatClient::displayStatus() {
  werase(status_win);
  box(status_win, 0, 0);

  if (!connected) {
    mvwprintw(status_win, 1, 1, "Status: Not connected");
  } else if (!authenticated) {
    mvwprintw(status_win, 1, 1, "Status: Connecting...");
  } else {
    mvwprintw(status_win, 1, 1, "Status: Connected");
  }
}

void ChatClient::connectToServer(const std::vector<nexilis::RoomInfo> &rooms) {
  if (rooms.empty()) {
    updateMessages("No rooms found, cannot join.");
    return;
  }

  m_room_id = rooms[0].getId();
  m_tcp_client->sendMessage(
      nexilis::client::Packet::Room::Management::join(m_tcp_client->getClientAPI(), m_room_id));
  authenticated = true;
}

void ChatClient::sendMessage(const std::string &message) {
  if (!m_tcp_client) {
    return;
  }

  m_tcp_client->sendMessage(
      nexilis::client::Packet::Room::Communicate::broadcast(m_tcp_client->getClientAPI(), message));
}

void ChatClient::pollRoomMessages() {
  if (!m_tcp_client || m_room_id == 0) {
    return;
  }

  auto &api = m_tcp_client->getClientAPI();
  if (!api.isInitialized()) {
    return;
  }

  auto *room = api.getRoom(m_room_id);
  if (!room) {
    return;
  }

  const auto &room_messages = room->getMessages();
  if (room_messages.size() <= m_last_message_count) {
    return;
  }

  for (size_t i = m_last_message_count; i < room_messages.size(); ++i) {
    const auto &communication = room_messages[i];

    std::string name = "unknown";
    const auto *sender = communication.getClient();
    if (sender) {
      if (sender->getId() == api.getClientId()) {
        name = client_username;
      } else if (!sender->getUsername().empty()) {
        name = sender->getUsername();
      } else {
        name = "user" + std::to_string(sender->getId());
      }
    }

    updateMessages("[" + name + "]: " + communication.getPayload());
  }

  m_last_message_count = room_messages.size();
}

void ChatClient::updateMessages(const std::string &message) {
  messages.push_back(message);

  // Keep only last 50 messages
  if (messages.size() > 50) {
    messages.erase(messages.begin());
  }
}

int main() {
  ChatClient client;
  client.run();
  return 0;
}
