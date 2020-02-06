#include "Simulation.h"

#include <cstdlib>
#include <unordered_map>

#include "Logger.h"

const std::unordered_map<std::string, Simulation::MemberMsgHandler_t>
    Simulation::MSG_HANDLERS = {
        {"CreateToken", &Simulation::onCreateToken},
        {"MoveToken", &Simulation::onMoveToken},
        {"DeleteToken", &Simulation::onDeleteToken},
        {"Chat", &Simulation::onChat},
        {"CreateDoodadLine", &Simulation::onCreateDoodadLine},
        {"ClearDoodads", &Simulation::onClearDoodads},
        {"ClearTokens", &Simulation::onClearTokens},
        {"TokenToggleFoe", &Simulation::onTokenToggleFoe},
        {"InitSession", &Simulation::onInitSession},
        {"SetUsername", &Simulation::onSetUsername},
        {"SetBuilding", &Simulation::onSetBuilding}};

const Simulation::Color Simulation::COLORS[Simulation::NUM_COLORS] = {
    {240, 50, 50},   // red
    {176, 30, 90},   // burgund
    {201, 20, 201},  // pink
    {120, 61, 196},  // purple
    {24, 100, 171},  // blue
    {24, 172, 171},  // turquoise
    {8, 127, 91},    // blue-green
    {92, 148, 13},   // red-green
    {217, 72, 15},   // orange
    {129, 96, 65},   // brown
    {201, 201, 30}   // yellow
};

Simulation::Simulation() : _next_id(0), _next_color(0) {
  _rand_seed = time(NULL);
}

Token *Simulation::tokenById(uint64_t id) {
  for (Token &t : _tokens) {
    if (t.id() == id) {
      return &t;
    }
  }
  return nullptr;
}

WebSocketServer::Response Simulation::onNewClient() {
  std::lock_guard<std::mutex> simulation_mutex_lock(_simulation_mutex);

  using nlohmann::json;
  std::string answer_str;
  try {
    // Assemble an init package
    json answer;
    answer["type"] = "Init";
    json data;

    std::vector<json> _encoded_tokens;
    for (Token &t : _tokens) {
      _encoded_tokens.emplace_back(t.serialize());
    }
    data["tokens"] = _encoded_tokens;

    std::vector<json> _encoded_doodads;
    for (DoodadLine &d : _doodad_lines) {
      _encoded_doodads.emplace_back(d.serialize());
    }
    data["doodads"] = _encoded_doodads;

    data["building"] = _building_json;

    data["nextId"] = _next_id;
    data["nextColor"] = _next_color;
    answer["data"] = data;
    answer_str = answer.dump();
  } catch (const std::exception &e) {
    LOG_ERROR << "Unable to assemble an init packet for the new client: "
              << e.what() << LOG_END;
  }

  return {answer_str, WebSocketServer::ResponseType::RETURN};
}

WebSocketServer::Response Simulation::onMessage(const std::string &msg) {
  std::lock_guard<std::mutex> simulation_mutex_lock(_simulation_mutex);
  using nlohmann::json;
  try {
    json j = json::parse(msg);
    std::string type = j.at("type");
    LOG_DEBUG << "Received a message of type " << type << " : " << j.dump()
              << LOG_END;
    std::unordered_map<std::string, MemberMsgHandler_t>::const_iterator
        handler_it = MSG_HANDLERS.find(type);
    if (handler_it != MSG_HANDLERS.end()) {
      // call the handler
      return (this->*(handler_it->second))(j);
    }

    WebSocketServer::Response r;
    r.type = WebSocketServer::ResponseType::RETURN;
    r.text = "Unknown message type " + type;
    LOG_WARN << "Received a message of unknown type " << type << LOG_END;
    return r;
  } catch (const std::exception &e) {
    LOG_ERROR << "Error when handling a msg from a client " << e.what() << "\n"
              << msg << LOG_END;
  }

  WebSocketServer::Response r;
  r.type = WebSocketServer::ResponseType::RETURN;
  r.text = "Error handling a message.";
  return r;
}

Simulation::Color Simulation::nextColor() {
  Color c = COLORS[_next_color];
  _next_color++;
  _next_color %= NUM_COLORS;
  return c;
}

WebSocketServer::Response Simulation::onCreateToken(const nlohmann::json &j) {
  if (!checkPermissions(j, Permissions::GAMEMASTER)) {
    nlohmann::json response;
    response["type"] = "Error";

    nlohmann::json resp_data;
    resp_data["msg"] = "Missing permissions.";
    resp_data["cause"] = j;

    response["data"] = resp_data;
    return {response.dump(), WebSocketServer::ResponseType::RETURN};
  }
  _tokens.emplace_back();
  float x = j.at("data").at("x");
  float y = j.at("data").at("y");
  Token &t = _tokens.back();
  t.x() = x;
  t.y() = y;
  t.id() = _next_id;
  t.radius() = 0.25;
  t.is_enemy() = false;
  Color c = nextColor();
  t.r() = c.r;
  t.g() = c.g;
  t.b() = c.b;
  _next_id++;

  nlohmann::json response;
  response["type"] = "CreateToken";
  response["data"] = t.serialize();

  return {response.dump(), WebSocketServer::ResponseType::BROADCAST};
}

WebSocketServer::Response Simulation::onMoveToken(const nlohmann::json &j) {
  nlohmann::json data = j.at("data");
  uint64_t id = data.at("id").get<uint64_t>();
  Token *t = tokenById(id);
  if (t != nullptr) {
    t->x() = data.at("x").get<float>();
    t->y() = data.at("y").get<float>();
  }
  return {"", WebSocketServer::ResponseType::FORWARD};
}

WebSocketServer::Response Simulation::onDeleteToken(const nlohmann::json &j) {
  if (!checkPermissions(j, Permissions::GAMEMASTER)) {
    nlohmann::json response;
    response["type"] = "Error";

    nlohmann::json resp_data;
    resp_data["msg"] = "Missing permissions.";
    resp_data["cause"] = j;

    response["data"] = resp_data;
    return {response.dump(), WebSocketServer::ResponseType::RETURN};
  }
  bool did_delete = false;
  nlohmann::json data = j.at("data");
  uint64_t id = data.at("id").get<uint64_t>();
  for (size_t i = 0; i < _tokens.size(); i++) {
    if (_tokens[i].id() == id) {
      LOG_DEBUG << "Deleted token with id " << id << LOG_END;
      std::iter_swap(_tokens.begin() + i,
                     _tokens.begin() + (_tokens.size() - 1));
      _tokens.erase(_tokens.end());
      did_delete = true;
      break;
    }
  }
  if (!did_delete) {
    LOG_WARN << "A client requested deletion of token " << id
             << " but no token with that id exists." << LOG_END;
  }
  return {"", WebSocketServer::ResponseType::FORWARD};
}

WebSocketServer::Response Simulation::onChat(const nlohmann::json &j) {
  using nlohmann::json;
  std::string uid = j.at("uid");
  std::string msg = j.at("data").at("message");
  std::string sender = "unknown";

  Player *player = getPlayer(uid);
  if (player) {
    sender = player->name;
  }
  if (msg[0] == '/') {
    WebSocketServer::Response resp;
    json resp_json = j;
    resp.type = WebSocketServer::ResponseType::BROADCAST;

    // The message is a command
    std::vector<std::string> parts = splitWs(msg);
    const std::string &cmd = parts[0];
    if (cmd == "/roll") {
      resp_json["data"]["message"] = cmdRollDice(sender, parts);
    } else if (cmd == "/rollp") {
      resp_json["data"]["message"] = cmdRollDice("You", parts);
      resp.type = WebSocketServer::ResponseType::RETURN;
    } else if (cmd == "/setname") {
      resp_json["data"]["message"] = cmdSetname(sender, uid, parts);
    } else if (cmd == "/help") {
      resp_json["data"]["message"] = cmdHelp(sender, uid, parts);
      resp.type = WebSocketServer::ResponseType::RETURN;
    } else if (cmd == "/gm") {
      if (player) {
        player->permissions = Permissions::GAMEMASTER;
      }
      resp.type = WebSocketServer::ResponseType::SILENCE;
    } else {
      resp_json["data"]["message"] = "Unknown command '" + parts[0] + "'";
      resp.type = WebSocketServer::ResponseType::RETURN;
    }

    if (resp.type == WebSocketServer::ResponseType::RETURN) {
      resp_json["data"]["sender"] = "The Server to you";
    }
    resp.text = resp_json.dump();
    return resp;
  } else if (msg.empty()) {
    return {"", WebSocketServer::ResponseType::SILENCE};
  }

  return {"", WebSocketServer::ResponseType::FORWARD};
}

WebSocketServer::Response Simulation::onCreateDoodadLine(
    const nlohmann::json &j) {
  if (!checkPermissions(j, Permissions::GAMEMASTER)) {
    nlohmann::json response;
    response["type"] = "Error";

    nlohmann::json resp_data;
    resp_data["msg"] = "Missing permissions.";
    resp_data["cause"] = j;

    response["data"] = resp_data;
    return {response.dump(), WebSocketServer::ResponseType::RETURN};
  }
  _doodad_lines.emplace_back();
  DoodadLine &d = _doodad_lines.back();
  d.deserialize(j.at("data"));
  return {"", WebSocketServer::ResponseType::FORWARD};
}

WebSocketServer::Response Simulation::onClearDoodads(const nlohmann::json &j) {
  if (!checkPermissions(j, Permissions::GAMEMASTER)) {
    nlohmann::json response;
    response["type"] = "Error";

    nlohmann::json resp_data;
    resp_data["msg"] = "Missing permissions.";
    resp_data["cause"] = j;

    response["data"] = resp_data;
    return {response.dump(), WebSocketServer::ResponseType::RETURN};
  }
  _doodad_lines.clear();
  return {"", WebSocketServer::ResponseType::FORWARD};
}

WebSocketServer::Response Simulation::onClearTokens(const nlohmann::json &j) {
  if (!checkPermissions(j, Permissions::GAMEMASTER)) {
    nlohmann::json response;
    response["type"] = "Error";

    nlohmann::json resp_data;
    resp_data["msg"] = "Missing permissions.";
    resp_data["cause"] = j;

    response["data"] = resp_data;
    return {response.dump(), WebSocketServer::ResponseType::RETURN};
  }
  _tokens.clear();
  return {"", WebSocketServer::ResponseType::FORWARD};
}

WebSocketServer::Response Simulation::onTokenToggleFoe(
    const nlohmann::json &j) {
  if (!checkPermissions(j, Permissions::GAMEMASTER)) {
    nlohmann::json response;
    response["type"] = "Error";

    nlohmann::json resp_data;
    resp_data["msg"] = "Missing permissions.";
    resp_data["cause"] = j;

    response["data"] = resp_data;
    return {response.dump(), WebSocketServer::ResponseType::RETURN};
  }
  nlohmann::json data = j.at("data");
  uint64_t id = data.at("id").get<uint64_t>();
  Token *t = tokenById(id);
  if (t != nullptr) {
    t->is_enemy() = !t->is_enemy();
  }

  return {"", WebSocketServer::ResponseType::FORWARD};
}

WebSocketServer::Response Simulation::onInitSession(const nlohmann::json &j) {
  using nlohmann::json;
  json req_data = j.at("data");
  std::string uid = req_data.at("uid");
  Player *player = getPlayer(uid);

  if (player == nullptr) {
    LOG_INFO << "A new player with uid " << uid << " connected." << LOG_END;
    // A new player connected
    _players.emplace_back();
    player = &_players.back();
    player->uid = uid;
    player->id = _players.size() - 1;
    player->permissions =
        _players.size() == 1 ? Permissions::GAMEMASTER : Permissions::PLAYER;
  } else {
    LOG_INFO << "A player with uid " << uid << " and name " << player->name
             << " reconnected." << LOG_END;
  }
  // encode the player
  json response;
  response["type"] = "Session";

  json resp_data;
  resp_data["id"] = player->id;
  resp_data["name"] = player->name;
  resp_data["permissions"] = (int)player->permissions;

  response["data"] = resp_data;

  return {response.dump(), WebSocketServer::ResponseType::RETURN};
}

WebSocketServer::Response Simulation::onSetUsername(const nlohmann::json &j) {
  std::string uid = j.at("uid");
  std::string newname = j.at("data").at("name");

  LOG_INFO << "The player with uid " << uid << " is now called " << newname
           << LOG_END;

  Player *p = getPlayer(uid);
  if (p) {
    p->name = newname;
  }
  return {"", WebSocketServer::ResponseType::SILENCE};
}

WebSocketServer::Response Simulation::onSetBuilding(const nlohmann::json &j) {
  if (!checkPermissions(j, Permissions::GAMEMASTER)) {
    nlohmann::json response;
    response["type"] = "Error";

    nlohmann::json resp_data;
    resp_data["msg"] = "Missing permissions.";
    resp_data["cause"] = j;

    response["data"] = resp_data;
    return {response.dump(), WebSocketServer::ResponseType::RETURN};
  }
  _building_json = j.at("data");
  return {"", WebSocketServer::ResponseType::FORWARD };
}

std::string Simulation::cmdRollDice(const std::string &who,
                                    const std::vector<std::string> &cmd) {
  std::ostringstream out;
  if (cmd.size() == 1) {
    out << who << " tried to roll an empty hand of dice.";
  } else {
    out << who << " rolled ";
    for (size_t i = 1; i < cmd.size(); i++) {
      try {
        int faces = std::stoi(cmd[i]);
        int val = (rand_r(&_rand_seed) % faces) + 1;
        out << val << " ";
      } catch (const std::exception &e) {
        out << "- ";
      }
    }
    out << "with dice ";
    for (size_t i = 1; i < cmd.size(); i++) {
      out << cmd[i];
      if (i + 1 < cmd.size()) {
        out << " ";
      }
    }
    out << ".";
  }
  return out.str();
}

std::string Simulation::cmdSetname(const std::string &who,
                                   const std::string &uid,
                                   const std::vector<std::string> &cmd) {
  std::stringstream s;
  for (size_t i = 1; i < cmd.size(); ++i) {
    s << cmd[i];
    if (i + 1 < cmd.size()) {
      s << " ";
    }
  }
  std::string newname = s.str();
  if (newname.size() > 0 && newname != "The Server") {
    Player *p = getPlayer(uid);
    if (p) {
      p->name = newname;
    }
    return who + " is now called " + newname;
  } else {
    return "'" + newname + " is an invalid new name for " + who;
  }
}

std::string Simulation::cmdHelp(const std::string &who, const std::string &uid,
                                const std::vector<std::string> &cmd) {
  std::stringstream s;
  s << "Commands: <br/>";
  s << "/roll <die 1> ... - Roll dice with a public result<br/>";
  s << "/rollp <die 1> ... - Roll dice with a private result<br/>";
  s << "/setname <newname> - Change your username.<br/>";

  return s.str();
}

std::vector<std::string> Simulation::splitWs(const std::string &s) {
  std::vector<std::string> parts;
  size_t begin = 0, end = 0;
  // Skip any leading whitespace
  while (begin < s.size() && std::isspace(s[begin])) {
    begin++;
  }

  // split the string
  while (begin < s.size()) {
    // At this point s[begin] is not whitespace
    // Search for the next whitespace
    end = begin + 1;
    while (end < s.size() && !std::isspace(s[end])) {
      end++;
    }

    // s[begin:end] is now the longest whitespace free substring starting at
    // begin
    parts.emplace_back(s.substr(begin, end - begin));

    begin = end + 1;
    // skip any whitespace until the next none whitespace
    while (begin < s.size() && std::isspace(s[begin])) {
      begin++;
    }
  }

  return parts;
}

Simulation::Player *Simulation::getPlayer(const std::string &uid) {
  for (Player &p : _players) {
    if (p.uid == uid) {
      return &p;
    }
  }
  return nullptr;
}

bool Simulation::checkPermissions(const nlohmann::json &packet,
                                  Permissions min_perm) {
  if (packet.contains("uid")) {
    std::string uid = packet.at("uid");
    Player *p = getPlayer(uid);
    if (p != nullptr) {
      return p->permissions == min_perm;
    }
  }
  return false;
}
