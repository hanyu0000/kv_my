# Common模块学习指南

## 学习目标
理解项目的基础工具模块，掌握线程安全编程、序列化等基础技术。

## 核心组件

### 1. LockQueue - 线程安全队列
**作用**：实现类似Go channel的线程安全队列

**核心特性**：
- 生产者-消费者模式
- 阻塞式读取
- 超时机制
- 线程安全

**使用场景**：
```cpp
// 创建队列
LockQueue<ApplyMsg> applyChan;

// 生产者：写入数据
applyChan.Push(msg);

// 消费者：读取数据（阻塞）
ApplyMsg msg = applyChan.Pop();

// 消费者：读取数据（超时）
ApplyMsg msg;
if (applyChan.timeOutPop(100, &msg)) {
    // 成功读取
} else {
    // 超时
}
```

### 2. 序列化工具
**作用**：将对象转换为字符串，便于网络传输和持久化

**核心方法**：
```cpp
// 序列化
std::string serializeToString() {
    std::stringstream ss;
    boost::archive::text_oarchive oa(ss);
    oa << *this;
    return ss.str();
}

// 反序列化
bool parseFromString(std::string str) {
    std::stringstream iss(str);
    boost::archive::text_iarchive ia(iss);
    ia >> *this;
    return true;
}
```

### 3. 调试工具
**作用**：提供调试和日志功能

**核心函数**：
```cpp
// 调试打印
DPrintf("Raft节点 %d 收到投票请求", nodeId);

// 断言
myAssert(condition, "条件不满足");

// 格式化字符串
std::string msg = format("节点%d在任期%d成为leader", nodeId, term);
```

### 4. 网络工具
**作用**：提供网络相关的工具函数

**核心功能**：
```cpp
// 检查端口是否可用
bool isReleasePort(unsigned short port);

// 获取可用端口
bool getReleasePort(short& port);
```

## 学习重点

### 1. 线程安全编程
- **互斥锁**：std::mutex的使用
- **条件变量**：std::condition_variable的使用
- **RAII**：资源管理的最佳实践

### 2. 序列化技术
- **boost::serialization**：C++序列化库
- **文本格式**：可读的序列化格式
- **二进制格式**：高效的序列化格式

### 3. 网络编程基础
- **端口管理**：端口检测和分配
- **网络地址**：IP地址处理

## 实践项目

### 项目1：实现简单的生产者-消费者
```cpp
#include "util.h"

// 生产者线程
void producer(LockQueue<int>& queue) {
    for (int i = 0; i < 10; i++) {
        queue.Push(i);
        sleep(1);
    }
}

// 消费者线程
void consumer(LockQueue<int>& queue) {
    while (true) {
        int value = queue.Pop();
        std::cout << "消费: " << value << std::endl;
    }
}

int main() {
    LockQueue<int> queue;
    
    std::thread p(producer, std::ref(queue));
    std::thread c(consumer, std::ref(queue));
    
    p.join();
    c.join();
    
    return 0;
}
```

### 项目2：序列化测试
```cpp
#include "util.h"

struct Person {
    std::string name;
    int age;
    
    template<class Archive>
    void serialize(Archive& ar, const unsigned int version) {
        ar & name & age;
    }
};

int main() {
    Person p1{"Alice", 25};
    
    // 序列化
    std::string data = p1.serializeToString();
    std::cout << "序列化结果: " << data << std::endl;
    
    // 反序列化
    Person p2;
    p2.parseFromString(data);
    std::cout << "反序列化结果: " << p2.name << ", " << p2.age << std::endl;
    
    return 0;
}
```

## 学习建议

### 1. 理解顺序
1. **先理解概念**：什么是线程安全、序列化
2. **再看实现**：理解LockQueue的实现原理
3. **最后实践**：自己实现简单的线程安全队列

### 2. 重点关注
- **线程安全**：多线程环境下的数据安全
- **性能考虑**：锁的粒度、内存分配
- **错误处理**：异常情况的处理



## 常见问题:

### Q1: 为什么需要线程安全队列？
A: 在多线程环境中，多个线程可能同时访问同一个数据结构，需要保证数据的一致性和安全性。

### Q2: 序列化有什么用？
A: 序列化可以将对象转换为字符串或字节流，便于网络传输、文件存储等。

### Q3: 如何选择合适的锁？
A: 根据访问模式选择合适的锁：互斥锁、读写锁、无锁数据结构等。