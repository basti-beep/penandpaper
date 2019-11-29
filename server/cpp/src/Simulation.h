#pragma once

#include <string>
#include <vector>

#include "Doodad.h"
#include "Token.h"
#include "WebSocketServer.h"

class Simulation {
  enum class Permissions { GAMEMASTER, PLAYER };

  struct Player {
    size_t id;
    std::string uid;
    Permissions permissions;
    std::string name;
  };

  struct Color {
    uint8_t r, g, b;
  };

 public:
  Simulation();

  WebSocketServer::Response onNewClient();
  WebSocketServer::Response onMessage(const std::string &msg);

  WebSocketServer::Response onCreateToken(const nlohmann::json &j);
  WebSocketServer::Response onMoveToken(const nlohmann::json &j);
  WebSocketServer::Response onDeleteToken(const nlohmann::json &j);
  WebSocketServer::Response onChat(const nlohmann::json &j);
  WebSocketServer::Response onCreateDoodadLine(const nlohmann::json &j);
  WebSocketServer::Response onClearDoodads(const nlohmann::json &j);
  WebSocketServer::Response onClearTokens(const nlohmann::json &j);
  WebSocketServer::Response onTokenToggleFoe(const nlohmann::json &j);
  WebSocketServer::Response onInitSession(const nlohmann::json &j);
  WebSocketServer::Response onSetUsername(const nlohmann::json &j);

 private:
  Token *tokenById(uint64_t id);

  std::vector<std::string> splitWs(const std::string &s);

  std::string cmdRollDice(const std::string &who, const std::vector<std::string> &cmd);

  Color nextColor();

  size_t _next_id;
  size_t _next_color;

  unsigned int _rand_seed;

  std::vector<Token> _tokens;
  std::vector<DoodadLine> _doodad_lines;

  std::vector<Player> _players;

  static const Color COLORS[10];
  static const int NUM_COLORS = 10;
};
