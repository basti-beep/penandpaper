/**
 * Copyright 2020 Florian Kramer
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "Wiki.h"

#include <cstdlib>
#include <nlohmann/json.hpp>

#include "Logger.h"
#include "Markdown.h"
#include "Util.h"

const std::string Wiki::IDX_COL = "numid";
const std::string Wiki::ID_COL = "id";
const std::string Wiki::PREDICATE_COL = "predicate";
const std::string Wiki::VALUE_COL = "value";
const std::string Wiki::FLAG_COL = "flags";
const std::string Wiki::TEXT_ATTR = "text";

const int Wiki::ATTR_INTERESTING = 1;
const int Wiki::ATTR_INHERITABLE = 2;
const int Wiki::ATTR_DATE = 4;

Wiki::Wiki(Database *db)
    : _db(db),
      _pages_table(
          _db->createTable("wiki", {{IDX_COL, DbDataType::AUTO_INCREMENT},
                                    {ID_COL, DbDataType::TEXT},
                                    {PREDICATE_COL, DbDataType::TEXT},
                                    {VALUE_COL, DbDataType::TEXT},
                                    {FLAG_COL, DbDataType::INTEGER}})),
      _root(&_pages_table) {
  // Build the entry tree
  DbCursor c = _pages_table.query();
  // initially create a list of entries
  while (!c.done()) {
    std::string id = c.col(1).text;
    if (_entry_map.count(id) == 0) {
      _entry_map.insert(
          std::make_pair(id, new Entry(id, nullptr, &_pages_table)));
      _ids_search_index.add(id, id);
    }
    c.next();
  }
  // Then build the tree and assign all attributes
  c.reset();
  while (!c.done()) {
    int64_t idx = c.col(0).integer;
    std::string id = c.col(1).text;
    std::string predicate = c.col(2).text;
    std::string value = c.col(3).text;
    int64_t flags = c.col(4).integer;
    auto it = _entry_map.find(id);
    if (it != _entry_map.end()) {
      if (predicate == "parent") {
        auto pit = _entry_map.find(value);
        if (pit != _entry_map.end()) {
          it->second->reparent(pit->second);
        } else {
          LOG_ERROR << "The entry " << id << " refers to an nonexistant parent "
                    << value << LOG_END;
        }
      } else {
        IndexedAttributeData d;
        d.idx = idx;
        d.data.value = value;
        d.data.flags = flags;
        it->second->loadAttribute(predicate, d);
      }
    } else {
      LOG_ERROR << "Wiki table modified during wiki loading." << LOG_END;
    }
    c.next();
  }

  // Reparent all parentless nodes to the root. This will also
  // ensure that every node will be deleted once this wiki instance
  // is destructed.
  for (auto &p : _entry_map) {
    if (p.second->parent() == nullptr) {
      p.second->reparent(&_root);
    }
  }
}

void Wiki::onRequest(const httplib::Request &req, httplib::Response &resp) {
  std::vector<std::string> parts = util::splitString(req.path, '/');
  if (parts.size() != 3 &&
      !(parts.size() == 2 && (parts[1] == "list" || parts[1] == "complete"))) {
    LOG_ERROR << "Invalid wiki request at path " << req.path << LOG_END;
    resp.status = 400;
    resp.body = "Invalid wiki request.";
    return;
  }
  LOG_INFO << "Wiki " << req.method << " request for " << req.path << LOG_END;
  std::string action = parts[1];
  if (action == "list") {
    handleList(resp);
  } else if (action == "complete") {
    handleCompleteEntity(req, resp);
  } else if (action == "get") {
    handleGet(parts[2], resp);
  } else if (action == "raw") {
    handleRaw(parts[2], resp);
  } else if (action == "save") {
    handleSave(parts[2], req, resp);
  } else if (action == "delete") {
    handleDelete(parts[2], req, resp);
  } else {
    LOG_ERROR << "Unknown wiki action " << action << " at " << req.path
              << LOG_END;
    resp.status = 400;
    resp.body = "Invalid wiki request.";
    return;
  }
}

void Wiki::handleCompleteEntity(const httplib::Request &req,
                                httplib::Response &resp) {
  using nlohmann::json;
  struct Completion {
    QGramIndex::Match match;
    size_t num_words_used;
    std::string replaces;

    bool operator<(const Completion &other) { return match < other.match; }
  };
  try {
    json jreq = json::parse(req.body);
    std::string context = jreq.at("context");
    std::vector<std::string> parts = util::splitStringWs(context);
    std::vector<Completion> results;
    for (size_t i = 1; i <= parts.size(); ++i) {
      std::string word;
      // use the i last words
      for (size_t j = 0; j < i; ++j) {
        std::string next = parts[parts.size() - 1 - j];
        if (j > 0) {
          next += " ";
        }
        word = next + word;
      }
      std::vector<QGramIndex::Match> subres = _ids_search_index.query(word);
      if (subres.empty()) {
        // This avoid a string such as 'Midgard z' matching Midgard. As z
        // is not contained in Midgard the match is rather strange.
        // For multi word matches this still works. E.g
        // `Albert Ein` is going to match `Albert Einstein` as `Ein` matches
        // `Albert Einstein`
        break;
      }
      for (const QGramIndex::Match &m : subres) {
        // Ignore matches with a very low score.
        if (m.score > 0.3) {
          results.push_back({m, i, word});
        }
      }
    }
    std::sort(results.begin(), results.end(),
              [](const Completion &c1, const Completion &c2) {
                return c1.match.score > c2.match.score;
              });

    json j = std::vector<json>();
    for (size_t i = 0; i < results.size(); ++i) {
      json completion;
      // replace the last offset characters
      // TODO: Alternatively it might be easier to send back a replacement
      // for the entirety of the context, due to the way codemirror does
      // autocompletion
      completion["offset"] = results[i].match.value.size();
      // Extract the part of the context that this replacement doesn't use
      std::string prefix =
          util::firstWords(context, parts.size() - results[i].num_words_used);
      // append the replacement to the unused part of the context
      completion["value"] = prefix + " [" + results[i].match.value + "](" +
                            results[i].match.value + ")";
      completion["name"] = results[i].match.value;
      completion["replaces"] =
          results[i].replaces + " " + std::to_string(results[i].match.score);
      j.push_back(completion);
    }

    resp.status = 200;
    resp.set_header("Content-Type", "application/json");
    resp.body = j.dump();
  } catch (const std::exception &e) {
    LOG_WARN << "Wiki: Error while handling a completion request: " << e.what()
             << LOG_END;
    resp.status = 400;
    resp.body = "Malformed completion request.";
  }
}

void Wiki::handleList(httplib::Response &resp) {
  using nlohmann::json;

  struct DfsLevel {
    Entry *entry;
    size_t child_index;
    json j;
  };

  // do a dfs on the entry tree
  std::vector<DfsLevel> dfs_stack;
  dfs_stack.push_back({&_root, 0, json()});
  dfs_stack.back().j["name"] = "root";
  dfs_stack.back().j["id"] = nullptr;
  dfs_stack.back().j["children"] = json::array();

  while (!dfs_stack.empty()) {
    DfsLevel &l = dfs_stack.back();
    if (l.child_index >= l.entry->children().size()) {
      // we are done with this node
      if (dfs_stack.size() == 1) {
        // we are done with the root node
        break;
      }
      DfsLevel &parent = dfs_stack[dfs_stack.size() - 2];
      parent.j["children"].push_back(l.j);
      dfs_stack.pop_back();
    } else {
      // the node has more children
      size_t ci = l.child_index;
      Entry *child = l.entry->children()[ci];
      l.child_index++;
      // this invalidates the reference l
      json j;
      j["name"] = child->name();
      j["id"] = child->id();
      j["children"] = json::array();
      dfs_stack.push_back({child, 0, j});
    }
  }

  resp.status = 200;
  resp.body = dfs_stack.back().j.dump();
}

void Wiki::handleGet(const std::string &id, httplib::Response &resp) {
  auto it = _entry_map.find(id);
  std::string cache_key = id + ":" + TEXT_ATTR;
  if (it != _entry_map.end()) {
    if (_markdown_cache.count(cache_key) > 0) {
      LOG_DEBUG << "Answering using the cache" << LOG_END;
      resp.status = 200;
      resp.body = _markdown_cache[cache_key];
    } else {
      LOG_DEBUG << "Loading the text and applying markdown" << LOG_END;
      std::string raw;
      const std::vector<IndexedAttributeData> *attr =
          it->second->getAttribute(TEXT_ATTR);
      if (attr != nullptr && !attr->empty()) {
        raw = attr->at(0).data.value;
        LOG_DEBUG << "Found the " << TEXT_ATTR << " attribute: '" << raw << "'"
                  << LOG_END;
      } else {
        LOG_WARN << "Entry " << id << " has no " << TEXT_ATTR << LOG_END;
      }
      Markdown m(raw);
      try {
        std::string parsed = m.process();
        if (_markdown_cache.size() > MAX_MARKDOWN_CACHE_SIZE) {
          // just erase any element.
          _markdown_cache.erase(_markdown_cache.begin());
        }
        _markdown_cache[cache_key] = parsed;
        resp.status = 200;
        resp.body = parsed;
      } catch (const std::exception &e) {
        resp.status = 200;
        resp.body = "Unable to parse the input markdown<br/>" + raw;
      }
    }
  } else {
    resp.status = 404;
    resp.body = "No such wiki entry";
  }
}

void Wiki::handleRaw(const std::string &id, httplib::Response &resp) {
  auto it = _entry_map.find(id);
  if (it != _entry_map.end()) {
    std::string raw;
    const std::vector<IndexedAttributeData> *attr =
        it->second->getAttribute(TEXT_ATTR);
    if (attr != nullptr && !attr->empty()) {
      std::string raw = attr->at(0).data.value;
    }
    resp.status = 200;
    resp.body = raw;
  } else {
    resp.status = 404;
    resp.body = "No such wiki entry";
  }
}

void Wiki::handleSave(const std::string &id, const httplib::Request &req,
                      httplib::Response &resp) {
  using nlohmann::json;
  if (id == "root") {
    resp.status = 400;
    resp.body = "`root` is not an allowed id.";
    return;
  }
  try {
    json jreq = json::parse(req.body);
    LOG_DEBUG << "Got json for save: " << jreq << LOG_END;
    std::vector<Attribute> attributes;
    std::string new_parent_id = "root";
    for (const auto &attr : jreq.items()) {
      Attribute a;
      a.predicate = attr.key();
      a.data.flags = 0;
      a.data.flags |=
          attr.value().at("isInteresting").get<bool>() ? ATTR_INTERESTING : 0;
      a.data.flags |=
          attr.value().at("isInheritable").get<bool>() ? ATTR_INHERITABLE : 0;
      a.data.flags |= attr.value().at("isDate").get<bool>() ? ATTR_DATE : 0;
      a.data.value = attr.value().at("value").get<std::string>();
      LOG_DEBUG << "Got attribute " << a.predicate << " " << a.data.value
                << LOG_END;
      if (a.predicate == "parent") {
        new_parent_id = a.data.value;
      }
      attributes.push_back(a);
    }
    Entry *parent = &_root;
    if (new_parent_id != "root") {
      parent = _entry_map[new_parent_id];
    }

    auto it = _entry_map.find(id);
    if (it != _entry_map.end()) {
      LOG_DEBUG << "Upated the " << TEXT_ATTR << " attribute on " << id
                << LOG_END;
      it->second->setAttributes(attributes);
      if (parent != it->second->parent()) {
        it->second->reparent(parent);
      }
    } else {
      LOG_DEBUG << "Created a new entry with id " << id << LOG_END;
      Entry *e = parent->addChild(id);
      e->setAttributes(attributes);
      _entry_map[id] = e;
    }
    auto mit = _markdown_cache.find(id + ":" + TEXT_ATTR);
    if (mit != _markdown_cache.end()) {
      _markdown_cache.erase(mit);
    }
    resp.status = 200;
    resp.body = "Save succesfull";
    return;
  } catch (const std::exception &e) {
    LOG_WARN << "Unable to process a save request: " << e.what() << LOG_END;
  }
  resp.status = 400;
  resp.body = "Invalid request";
}

void Wiki::handleDelete(const std::string &id, const httplib::Request &req,
                        httplib::Response &resp) {
  auto it = _entry_map.find(id);
  if (it != _entry_map.end()) {
    delete it->second;
    _entry_map.erase(it);
    _ids_search_index.remove(id);
    resp.status = 200;
    resp.body = "Deletion succesfull";
    return;
  }
  resp.status = 400;
  resp.body = "Unable to delete the entry.";
}

// =============================================================================
// Entry
// =============================================================================

Wiki::Entry::Entry(Table *storage)
    : _id("root"), _parent(nullptr), _storage(storage) {}

Wiki::Entry::Entry(const std::string &id, Entry *parent, Table *storage)
    : _id(id), _parent(parent), _storage(storage) {
  if (_parent != nullptr) {
    _parent->_children.push_back(this);
  }
}

Wiki::Entry::~Entry() {
  while (_children.size() > 0) {
    delete _children[0];
  }
  reparent(nullptr);
}

Wiki::Entry *Wiki::Entry::addChild(std::string child_id) {
  return new Entry(child_id, this, _storage);
}

void Wiki::Entry::reparent(Entry *new_parent) {
  if (_parent != nullptr) {
    _parent->_children.erase(
        std::remove(_parent->_children.begin(), _parent->_children.end(), this),
        _parent->_children.end());
  }
  _parent = new_parent;
  if (_parent != nullptr) {
    _parent->_children.push_back(this);
  }
}

Wiki::Entry *Wiki::Entry::parent() { return _parent; }

const std::string &Wiki::Entry::name() const {
  if (hasAttribute("name")) {
    return (*getAttribute("name"))[0].data.value;
  }
  return _id;
}

const std::string &Wiki::Entry::id() const { return _id; }

const std::vector<Wiki::Entry *> &Wiki::Entry::children() const {
  return _children;
}

const std::vector<Wiki::IndexedAttributeData> *Wiki::Entry::getAttribute(
    const std::string &predicate) const {
  auto it = _attributes.find(predicate);
  if (it != _attributes.end()) {
    return &(it->second);
  }
  return nullptr;
}

void Wiki::Entry::loadAttribute(const std::string &predicate,
                                const IndexedAttributeData &value) {
  auto it = _attributes.find(predicate);
  if (it == _attributes.end()) {
    _attributes.insert(
        std::pair<std::string, std::vector<IndexedAttributeData>>(predicate,
                                                                  {value}));
  } else {
    // This only compares the value, none of the other properties
    if (std::find(it->second.begin(), it->second.end(), value) !=
        it->second.end()) {
      // The attribute already exists. This is not necessarily an error
      LOG_WARN << "Duplicate attribute " << _id << " - " << predicate << " - "
               << value.data.value << " while loading." << LOG_END;
      return;
    } else {
      it->second.push_back(value);
    }
  }
}

void Wiki::Entry::addAttribute(const std::string &predicate,
                               const AttributeData &value) {
  auto it = _attributes.find(predicate);
  IndexedAttributeData d;
  d.data = value;
  if (it == _attributes.end()) {
    int64_t idx = writeAttribute(predicate, value);
    d.idx = idx;
    _attributes[predicate].push_back(d);
  } else {
    if (std::find(it->second.begin(), it->second.end(), d) !=
        it->second.end()) {
      // The attribute already exists. This is not necessarily an error
      return;
    } else {
      int64_t idx = writeAttribute(predicate, value);
      d.idx = idx;
      it->second.push_back(d);
      // Write the attribute to the persistent storage.
    }
  }
}

void Wiki::Entry::setAttributes(const std::vector<Attribute> &attributes) {
  // For now simply rewrite all our existing entries with new ones and delete
  // any that are to many.
  size_t num_attributes = 0;
  for (auto it : _attributes) {
    num_attributes += it.second.size();
  }
  size_t to_overwrite = std::min(num_attributes, attributes.size());
  LOG_DEBUG << "Whill overwrite " << to_overwrite << " of the current "
            << num_attributes << " to store the new " << attributes.size()
            << " attributes " << LOG_END;
  // out position in the attributes vector
  size_t new_pos = 0;
  std::vector<std::string> erased_predicates;
  for (auto it : _attributes) {
    for (auto vit = it.second.begin(); vit != it.second.end(); ++vit) {
      if (new_pos < attributes.size()) {
        // update
        const Attribute &a = attributes[new_pos];
        LOG_DEBUG << "Overwriting attribute " << it.first << " "
                  << vit->data.value << LOG_END;
        // update the database
        _storage->update({{PREDICATE_COL, a.predicate},
                          {VALUE_COL, a.data.value},
                          {FLAG_COL, a.data.flags}},
                         DbCondition(IDX_COL, DBCT::EQ, vit->idx));
        // update out cached version
        vit->data = a.data;
        new_pos++;
      } else {
        // delete
        // Delete the remainder in the database
        for (auto dit = vit; dit != it.second.end(); ++dit) {
          LOG_DEBUG << "Deleting attribute " << it.first << " "
                    << vit->data.value << LOG_END;
          _storage->erase(DbCondition(IDX_COL, DBCT::EQ, dit->idx));
        }
        // delete the remainder in the cache
        it.second.erase(vit, it.second.end());
        break;
      }
    }
    if (it.second.empty()) {
      erased_predicates.push_back(it.first);
    }
  }
  for (size_t i = new_pos; i < attributes.size(); ++i) {
    LOG_DEBUG << "Adding a new attribute " << attributes[i].predicate << " "
              << attributes[i].data.value << LOG_END;
    // create
    addAttribute(attributes[i].predicate, attributes[i].data);
  }

  // Delete all empty predicates from the cache
  for (const std::string &predicate : erased_predicates) {
    LOG_DEBUG << "The predicate " << predicate << " has no valuse, deleting it"
              << LOG_END;
    _attributes.erase(predicate);
  }
}

int64_t Wiki::Entry::writeAttribute(const std::string &predicate,
                                    const AttributeData &value) {
  _storage->insert({{ID_COL, _id},
                    {PREDICATE_COL, predicate},
                    {VALUE_COL, value.value},
                    {FLAG_COL, value.flags}});
  DbCursor c =
      _storage->query(DbCondition(ID_COL, DBCT::EQ, _id) &&
                      DbCondition(PREDICATE_COL, DBCT::EQ, predicate) &&
                      DbCondition(VALUE_COL, DBCT::EQ, value.value));
  if (c.done()) {
    return -1;
  }
  return c.col(0).integer;
}

void Wiki::Entry::updateAttribute(int64_t idx, const std::string &new_predicate,
                                  const AttributeData &new_value) {
  _storage->update({{ID_COL, _id},
                    {PREDICATE_COL, new_predicate},
                    {VALUE_COL, new_value.value},
                    {FLAG_COL, new_value.flags}},
                   DbCondition(IDX_COL, DBCT::EQ, idx));
}

void Wiki::Entry::removeAttribute(const std::string &predicate) {
  if (_attributes.find(predicate) != _attributes.end()) {
    _attributes.erase(predicate);
    _storage->erase(DbCondition(ID_COL, DBCT::EQ, _id) &&
                    DbCondition(PREDICATE_COL, DBCT::EQ, predicate));
  }
}

void Wiki::Entry::removeAttribute(const std::string &predicate,
                                  const std::string &value) {
  auto it = _attributes.find(predicate);
  if (it != _attributes.end()) {
    IndexedAttributeData d;
    d.data.value = value;
    // Figure out which entries to remove.
    auto sit = std::remove(it->second.begin(), it->second.end(), d);
    auto vit = sit;
    // erase the entries from the database
    while (vit != it->second.end()) {
      _storage->erase(DbCondition(IDX_COL, DBCT::EQ, vit->idx));
      ++vit;
    }
    it->second.erase(sit, it->second.end());
  }
}

bool Wiki::Entry::hasAttribute(const std::string &predicate) const {
  return _attributes.count(predicate) > 0;
}

bool Wiki::Entry::hasAttribute(const std::string &predicate,
                               const std::string &value) const {
  auto it = _attributes.find(predicate);
  IndexedAttributeData d;
  d.data.value = value;
  if (it != _attributes.end()) {
    return std::find(it->second.begin(), it->second.end(), d) !=
           it->second.end();
  }
  return false;
}
