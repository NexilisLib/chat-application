#include <atomic>
#include <chrono>
#include <clocale>
#include <cwctype>
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

void appendUtf8(std::string &out, wint_t wc) {
  if (wc < 0x80) {
    out.push_back(static_cast<char>(wc));
  } else if (wc < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (wc >> 6)));
    out.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
  } else if (wc < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (wc >> 12)));
    out.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (wc >> 18)));
    out.push_back(static_cast<char>(0x80 | ((wc >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
  }
}

void popBackUtf8(std::string &s) {
  if (s.empty()) {
    return;
  }
  size_t bytes = 1;
  while (bytes <= s.size() && bytes < 4 &&
         (static_cast<unsigned char>(s[s.size() - bytes]) & 0xC0) == 0x80) {
    ++bytes;
  }
  s.erase(s.size() - bytes);
}

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
  bool pollRoomMessages();
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

  // Redraw flags
  bool m_messages_dirty = true;
  bool m_input_dirty = true;
  bool m_status_dirty = true;
};

ChatClient::ChatClient() : connected(false), authenticated(false) {
  setlocale(LC_ALL, "");
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

    int result;
    wint_t wc;
    result = wget_wch(input_win, &wc);
    if (result == ERR) {
      continue;
    }

    if (result == KEY_CODE_YES) {
      if (wc == KEY_BACKSPACE || wc == KEY_DC) {
        popBackUtf8(value);
      }
      continue;
    }

    if (wc == L'\n' || wc == L'\r') {
      return value.empty() ? default_value : value;
    }
    if (wc == 127 || wc == 8) {
      popBackUtf8(value);
      continue;
    }
    if (wc >= 32 && iswprint(wc)) {
      if (value.size() < 64) {
        appendUtf8(value, wc);
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
  m_status_dirty = true;

  // Main chat loop
  while (true) {
    handleInput();
    updateUI();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

void ChatClient::handleInput() {
  wint_t wc;
  int result = wget_wch(input_win, &wc);
  if (result == ERR) {
    return;
  }

  if (result == KEY_CODE_YES) {
    if (wc == KEY_BACKSPACE || wc == KEY_DC) {
      popBackUtf8(current_message);
      m_input_dirty = true;
    }
    return;
  }

  if (wc == L'\n' || wc == L'\r') {
    if (!current_message.empty()) {
      sendMessage(current_message);
      current_message.clear();
      m_input_dirty = true;
    }
    return;
  }
  if (wc == 127 || wc == 8) {
    popBackUtf8(current_message);
    m_input_dirty = true;
    return;
  }
  if (wc == 'q' || wc == 'Q') {
    endwin();
    exit(0);
  }
  if (wc >= 32 && iswprint(wc)) {
    if (current_message.size() < 256) {
      appendUtf8(current_message, wc);
      m_input_dirty = true;
    }
  }
}

void ChatClient::updateUI() {
  bool messages_changed = pollRoomMessages();

  if (messages_changed || m_messages_dirty) {
    displayMessages();
    m_messages_dirty = false;
  }
  if (m_input_dirty) {
    displayInputFields();
    m_input_dirty = false;
  }
  if (m_status_dirty) {
    displayStatus();
    m_status_dirty = false;
  }

  // Flush all windows in one pass. The input window is refreshed last so the
  // hardware cursor stays in the message input field.
  wnoutrefresh(message_win);
  wnoutrefresh(status_win);
  wnoutrefresh(input_win);
  doupdate();
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
  m_tcp_client->sendMessage(
      nexilis::client::Packet::Set::General::username(m_tcp_client->getClientAPI(), client_username));
  authenticated = true;
  m_status_dirty = true;
}

void ChatClient::sendMessage(const std::string &message) {
  if (!m_tcp_client) {
    return;
  }

  m_tcp_client->sendMessage(
      nexilis::client::Packet::Room::Communicate::broadcast(m_tcp_client->getClientAPI(), message));
}

bool ChatClient::pollRoomMessages() {
  if (!m_tcp_client || m_room_id == 0) {
    return false;
  }

  auto &api = m_tcp_client->getClientAPI();
  if (!api.isInitialized()) {
    return false;
  }

  auto *room = api.getRoom(m_room_id);
  if (!room) {
    return false;
  }

  const auto &room_messages = room->getMessages();
  if (room_messages.size() <= m_last_message_count) {
    return false;
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
  return true;
}

void ChatClient::updateMessages(const std::string &message) {
  messages.push_back(message);

  // Keep only last 50 messages
  if (messages.size() > 50) {
    messages.erase(messages.begin());
  }
  m_messages_dirty = true;
}

int main() {
  ChatClient client;
  client.run();
  return 0;
}
