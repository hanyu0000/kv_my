#include "util.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iomanip>

// 自定义断言函数实现
// 如果条件为false，则输出错误信息并退出程序
void myAssert(bool condition, std::string message) {
  if (!condition) {
    std::cerr << "Error: " << message << std::endl;
    std::exit(EXIT_FAILURE);
  }
}

// 获取当前时间点
// 使用高精度时钟，提供纳秒级精度
std::chrono::_V2::system_clock::time_point now() { return std::chrono::high_resolution_clock::now(); }

// 获取随机化的选举超时时间
// 用于Raft算法，避免选举冲突
std::chrono::milliseconds getRandomizedElectionTimeout() {
  std::random_device rd;   // 随机设备，用于生成种子
  std::mt19937 rng(rd());  // Mersenne Twister随机数生成器
  // 创建均匀分布，范围在最小和最大选举超时之间
  std::uniform_int_distribution<int> dist(minRandomizedElectionTime, maxRandomizedElectionTime);
  // 生成随机数并转换为毫秒
  return std::chrono::milliseconds(dist(rng));
}

// 睡眠指定毫秒数
// 使用当前线程睡眠，不占用CPU
void sleepNMilliseconds(int N) { std::this_thread::sleep_for(std::chrono::milliseconds(N)); };

bool getReleasePort(short &port) {
  short num = 0;
  while (!isReleasePort(port) && num < 30) {
    ++port;
    ++num;
  }

  if (num >= 30) {
    port = -1;
    return false;
  }

  return true;
}

bool isReleasePort(unsigned short usPort) {
  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

  sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(usPort);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  int ret = ::bind(s, (sockaddr *)&addr, sizeof(addr));

  if (ret != 0) {
    close(s);
    return false;
  }

  close(s);
  return true;
}

void DPrintf(const char *format, ...) {
  if (Debug) {
    // 获取当前的日期
    time_t now = time(nullptr);
    tm *nowtm = localtime(&now);
    // 设置可变参数列表
    va_list args;
    va_start(args, format);

    std::printf("[%d-%d-%d-%d-%d-%d] ", nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday, nowtm->tm_hour,
                nowtm->tm_min, nowtm->tm_sec);

    std::vprintf(format, args);
    std::printf("\n");
    // 清理可变参数列表
    va_end(args);
  }
}