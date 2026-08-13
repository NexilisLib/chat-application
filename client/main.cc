#include <chrono>
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
  void setupUI();
  void handleInput();
  void updateUI();
  void connectToServer();
  void sendMessage(const std::string &message);
  void displayMessages();
  void displayInputFields();
  void displayStatus();

private:
  void handleServerMessages();
  void updateMessages(const std::string &message);

private:
  nexilis::ProtocolManager m_protocol_manager;
  nexilis::TCPClient m_tcp_client;

  // UI elements
  WINDOW *input_win;
  WINDOW *message_win;
  WINDOW *status_win;

  // Input fields
  std::string server_ip;
  std::string server_password;
  std::string client_username;

  // Messages
  std::vector<std::string> messages;
};

ChatClient::ChatClient()
    : m_tcp_client(&m_protocol_manager, "127.0.0.1", "password") {

  // Initialize ncurses
  initscr();
  cbreak();
  noecho();
  nodelay(stdscr, TRUE);
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

  box(message_win, 0, 0);
  box(input_win, 0, 0);
  box(status_win, 0, 0);

  refresh();
  wrefresh(message_win);
  wrefresh(input_win);
  wrefresh(status_win);
}

void ChatClient::run() {

  std::vector<nexilis::RoomInfo> rooms;
  std::atomic<bool> ready = false;
  std::mutex mtx;
  auto start_client = nexilis::startClient(m_tcp_client, rooms, ready, mtx);
  start_client.detach();

  // First display input fields
  displayInputFields();
  wrefresh(input_win);

  // Get user input for connection
  bool input_complete = false;
  int current_field = 0; // 0 = server IP, 1 = password, 2 = username

  while (!input_complete) {
    handleInput();
    updateUI();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Simple input collection logic for demo purposes
    if (current_field == 0) {
      server_ip = "127.0.0.1";
      current_field = 1;
    } else if (current_field == 1) {
      server_password = "password";
      current_field = 2;
    } else if (current_field == 2) {
      client_username = "user";
      current_field = 3;
      input_complete = true;
    }
  }

  // Connect to server
  connectToServer();

  // Main chat loop
  while (true) {
    handleInput();
    updateUI();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void ChatClient::handleInput() {
  int ch = wgetch(input_win);

  // Handle key presses
  switch (ch) {
  case KEY_UP:
    // Handle up arrow
    break;
  case KEY_DOWN:
    // Handle down arrow
    break;
  case '\n': // Enter key
    // For now, just display a simple message
    updateMessages("Message sent from " + client_username);
    break;
  case 'q':
  case 'Q':
    endwin();
    exit(0);
    break;
  }
}

void ChatClient::updateUI() {
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

  // Display messages
  for (size_t i = 0; i < messages.size() && i < LINES - 10; ++i) {
    mvwprintw(message_win, i + 1, 1, "%s", messages[i].c_str());
  }

  wrefresh(message_win);
}

void ChatClient::displayInputFields() {
  werase(input_win);
  box(input_win, 0, 0);

  mvwprintw(input_win, 1, 1, "Server IP: %s", server_ip.c_str());
  mvwprintw(input_win, 2, 1, "Password: %s", server_password.c_str());
  mvwprintw(input_win, 3, 1, "Username: %s", client_username.c_str());
  mvwprintw(input_win, 5, 1, "Press Enter to send message");
  mvwprintw(input_win, 6, 1, "Press 'q' to quit");

  wrefresh(input_win);
}

void ChatClient::displayStatus() {
  werase(status_win);
  box(status_win, 0, 0);

  wrefresh(status_win);
}

void ChatClient::connectToServer() {

  auto &rooms = m_tcp_client.getClientAPI().getActiveRooms();

  m_tcp_client.sendMessage(
      nexilis::client::Packet::Room::Management::join(rooms[0].getId()));
}

void ChatClient::sendMessage(const std::string &message) {

  m_tcp_client.sendMessage(
      nexilis::client::Packet::Room::Communicate::broadcast(message));

  updateMessages("[" + client_username + "]: " + message);
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
