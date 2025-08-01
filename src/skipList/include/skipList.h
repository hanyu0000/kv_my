#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>

// STORE_FILE：指定持久化存储文件的路径，跳表可能会将数据写入磁盘
#define STORE_FILE "store/dumpFile"
// delimiter：用于分隔键值对（在持久化或输出中可能用 key:value 格式）
static std::string delimiter = ":";

template <typename K, typename V>
class Node {
 public:
  Node() {}
  Node(K k, V v, int);
  ~Node();

  K get_key() const;
  V get_value() const;
  void set_value(V);
  Node<K, V> **forward;
  int node_level;

 private:
  K key;
  V value;
};

template <typename K, typename V>
Node<K, V>::Node(const K k, const V v, int level) {
  this->key = k;
  this->value = v;
  this->node_level = level;

  this->forward = new Node<K, V> *[level + 1];
  memset(this->forward, 0, sizeof(Node<K, V> *) * (level + 1));
}

template <typename K, typename V>
Node<K, V>::~Node() {
  delete[] forward;  // delete[] 匹配之前的 new[]
};

template <typename K, typename V>
K Node<K, V>::get_key() const {
  return key;
};

template <typename K, typename V>
V Node<K, V>::get_value() const {
  return value;
};

template <typename K, typename V>
void Node<K, V>::set_value(V value) {
  this->value = value;
};

// 序列化跳表中的数据（键和值）
template <typename K, typename V>
class SkipListDump {
 public:
  friend class boost::serialization::access;  // Boost 序列化库访问本类的私有成员

  template <class Archive>
  void serialize(Archive &ar, const unsigned int version) {
    ar & keyDumpVt_;
    ar & valDumpVt_;
  }
  std::vector<K> keyDumpVt_;
  std::vector<V> valDumpVt_;

 public:
  void insert(const Node<K, V> &node);
};

// 通用跳表类模板 SkipList<K, V>，并封装了插入、删除、查找、序列化等功能
template <typename K, typename V>
class SkipList {
 public:
  SkipList(int);
  ~SkipList();

  int get_random_level();

  Node<K, V> *create_node(K, V, int);

  int insert_element(K, V);
  void display_list();
  bool search_element(K, V &value);
  void delete_element(K);
  void insert_set_element(K &, V &);
  std::string dump_file();
  void load_file(const std::string &dumpStr);
  // 递归删除节点
  void clear(Node<K, V> *);
  int size();

 private:
  void get_key_value_from_string(const std::string &str, std::string *key, std::string *value);
  bool is_valid_string(const std::string &str);

 private:
  int _max_level;
  int _skip_list_level;
  Node<K, V> *_header;

  std::ofstream _file_writer;
  std::ifstream _file_reader;

  int _element_count;
  std::mutex _mtx;
};
// Insert given key and value in skip list
// return 1 means element exists
// return 0 means insert successfully
/*
                           +------------+
                           |  insert 50 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |                      insert +----+
level 3         1+-------->10+---------------> | 50 |          70       100
                                               |    |
                                               |    |
level 2         1          10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 1         1    4     10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 0         1    4   9 10         30   40  | 50 |  60      70       100
                                               +----+

*/

template <typename K, typename V>
bool SkipList<K, V>::insert_element(const K key, const V value) {
  std::lock_guard<std::mutex> lock(_mtx);  // 自动加/解锁

  Node<K, V> *current = this->_header;
  // 自动初始化为 nullptr
  std::vector<Node<K, V> *> update(_max_level + 1, nullptr);

  // 从当前最大层向下找插入点
  for (int i = _skip_list_level; i >= 0; --i) {
    while (current->forward[i] != nullptr && current->forward[i]->get_key() < key) {
      current = current->forward[i];
    }
    update[i] = current;
  }

  current = current->forward[0];

  if (current != nullptr && current->get_key() == key) {
    // key 已存在：根据需求可以选择更新 value
    // current->set_value(value); // 如果想覆盖就解开注释
    std::cout << "key: " << key << ", exists" << std::endl;
    return false;  // 未插入新元素
  }

  // 需要插入新节点
  int random_level = get_random_level();
  if (random_level > _skip_list_level) {
    for (int i = _skip_list_level + 1; i <= random_level; ++i) {
      update[i] = _header;
    }
    _skip_list_level = random_level;
  }

  Node<K, V> *inserted_node = create_node(key, value, random_level);
  for (int i = 0; i <= random_level; ++i) {
    inserted_node->forward[i] = update[i]->forward[i];
    update[i]->forward[i] = inserted_node;
  }
  std::cout << "Successfully inserted key:" << key << ", value:" << value << std::endl;
  ++_element_count;
  return true;
}

// 按层遍历跳表，把每层的 key:value 打印出来
template <typename K, typename V>
void SkipList<K, V>::display_list() {
  for (int i = 0; i <= _skip_list_level; i++) {
    Node<K, V> *node = this->_header->forward[i];
    std::cout << "Level " << i << ": ";
    while (node != nullptr) {
      std::cout << node->get_key() << ":" << node->get_value() << ";";
      node = node->forward[i];
    }
    std::cout << '\n';
  }
}

// 把跳表内容序列化成一个字符串
template <typename K, typename V>
std::string SkipList<K, V>::dump_file() {
  std::lock_guard<std::mutex> lock(_mtx);

  SkipListDump<K, V> dumper;
  Node<K, V> *node = _header->forward[0];
  while (node != nullptr) {
    dumper.keyDumpVt_.push_back(node->get_key());
    dumper.valDumpVt_.push_back(node->get_value());
    node = node->forward[0];
  }
  std::stringstream ss;
  boost::archive::text_oarchive oa(ss);
  oa << dumper;
  return ss.str();
}

template <typename K, typename V>
void SkipList<K, V>::load_file(const std::string &dumpStr) {
  if (dumpStr.empty()) return;
  std::lock_guard<std::mutex> lock(_mtx);
  // 先清理旧数据
  for (int i = 0; i <= _skip_list_level; ++i) {
    _header->forward[i] = nullptr;
  }
  _skip_list_level = 0;
  _element_count = 0;

  SkipListDump<K, V> dumper;
  std::stringstream iss(dumpStr);  // 将字符串包装成输入流，供 Boost 反序列化使用
  try {
    boost::archive::text_iarchive ia(iss);
    ia >> dumper;
  } catch (const std::exception &e) {
    std::cerr << "load_file failed during deserialization: " << e.what() << std::endl;
    return;
  }

  // 插入反序列化得到的内容（key/value 对应）
  for (size_t i = 0; i < dumper.keyDumpVt_.size(); ++i) {
    insert_element(dumper.keyDumpVt_[i], dumper.valDumpVt_[i]);
  }
}

template <typename K, typename V>
int SkipList<K, V>::size() {
  return _element_count;
}

template <typename K, typename V>
void SkipList<K, V>::get_key_value_from_string(const std::string &str, std::string *key, std::string *value) {
  if (!is_valid_string(str)) return;

  *key = str.substr(0, str.find(delimiter));
  *value = str.substr(str.find(delimiter) + 1, str.length());
}

template <typename K, typename V>
bool SkipList<K, V>::is_valid_string(const std::string &str) {
  if (str.empty()) return false;

  if (str.find(delimiter) == std::string::npos) return false;

  return true;
}

template <typename K, typename V>
void SkipList<K, V>::delete_element(K key) {
  std::lock_guard<std::mutex> lock(_mtx);

  Node<K, V> *current = _header;
  std::vector<Node<K, V> *> update(_max_level + 1, nullptr);

  for (int i = _skip_list_level; i >= 0; --i) {
    while (current->forward[i] != nullptr && current->forward[i]->get_key() < key) {
      current = current->forward[i];
    }
    update[i] = current;
  }

  current = current->forward[0];

  if (current != nullptr && current->get_key() == key) {
    for (int i = 0; i <= _skip_list_level; ++i) {
      if (update[i]->forward[i] != current) break;
      update[i]->forward[i] = current->forward[i];
    }

    while (_skip_list_level > 0 && _header->forward[_skip_list_level] == nullptr) {
      --_skip_list_level;
    }

    std::cout << "Successfully deleted key " << key << std::endl;
    delete current;
    --_element_count;
  }
}

/**
 * 作用与insert_element相同类似
 * insert_element是插入新元素
 * insert_set_element是插入元素，如果元素存在则改变其值
 */
template <typename K, typename V>
void SkipList<K, V>::insert_set_element(K &key, V &value) {
  V oldValue;
  if (search_element(key, oldValue)) delete_element(key);
  insert_element(key, value);
}

template <typename K, typename V>
bool SkipList<K, V>::search_element(K key, V &value) {
  std::lock_guard<std::mutex> lock(_mtx);

  Node<K, V> *current = _header;

  for (int i = _skip_list_level; i >= 0; --i) {
    while (current->forward[i] != nullptr && current->forward[i]->get_key() < key) {
      current = current->forward[i];
    }
  }
  current = current->forward[0];
  if (current != nullptr && current->get_key() == key) {
    value = current->get_value();
    std::cout << "Found key: " << key << ", value: " << value << std::endl;
    return true;
  }
  std::cout << "Not Found Key: " << key << std::endl;
  return false;
}

template <typename K, typename V>
void SkipListDump<K, V>::insert(const Node<K, V> &node) {
  keyDumpVt_.emplace_back(node.get_key());
  valDumpVt_.emplace_back(node.get_value());
}

// 跳表 SkipList 类的构造函数，主要作用是初始化跳表的状态和头节点
template <typename K, typename V>
SkipList<K, V>::SkipList(int max_level) {
  this->_max_level = max_level;
  this->_skip_list_level = 0;
  this->_element_count = 0;

  K k;
  V v;
  this->_header = new Node<K, V>(k, v, _max_level);
};

template <typename K, typename V>
SkipList<K, V>::~SkipList() {
  if (_file_writer.is_open()) _file_writer.close();

  if (_file_reader.is_open()) _file_reader.close();
  // 递归删除跳表链条
  if (_header->forward[0] != nullptr) clear(_header->forward[0]);

  delete (_header);
}

template <typename K, typename V>
void SkipList<K, V>::clear(Node<K, V> *cur) {
  if (cur->forward[0] != nullptr) clear(cur->forward[0]);
  delete (cur);
}

// 生成随机层数
template <typename K, typename V>
int SkipList<K, V>::get_random_level() {
  int k = 1;
  while (rand() % 2) {
    k++;
  }
  k = (k < _max_level) ? k : _max_level;
  return k;
};

#endif  // SKIPLIST_H