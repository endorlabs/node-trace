#ifndef ENDOR_MONITOR_H_
#define ENDOR_MONITOR_H_

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "src/ast/ast.h"
#include "src/base/functional.h"
#include "src/execution/frames.h"
#include "src/handles/handles.h"
#include "src/objects/js-function.h"
#include "src/objects/js-objects.h"
#include "src/objects/object-type.h"
#include "src/objects/objects.h"
#include "src/objects/script.h"
#include "src/objects/string.h"

// #define MONITOR_BUFFER_SIZE 65536
#define MONITOR_BUFFER_SIZE 2048

enum class SerializationType { FILE, API };

struct FunctionDef {
  uint32_t id;
  int file_index;
  int position_start;
  int position_end;
  int line;
  int column;
  std::string name;
  bool is_constructor;
  std::string path;
  long long timestamp_ms;

  bool operator==(const FunctionDef& other) const { return id == other.id; }

  std::size_t Hash() const { return std::hash<uint32_t>{}(id); }
};

struct FunctionDefHash {
  std::size_t operator()(const FunctionDef& id) const { return id.Hash(); }
};

struct FunctionCall {
  uint32_t caller;
  uint32_t callee;
  long long timestamp_ms;

  bool operator==(const FunctionCall& other) const {
    return callee == other.callee && caller == other.caller;
  }

  std::size_t Hash() const { return v8::base::hash_combine(callee, caller); }
};

struct FunctionCallHash {
  std::size_t operator()(const FunctionCall& id) const { return id.Hash(); }
};

class Serializer {
 protected:
  std::vector<char> buffer;
  size_t bufferPos;
  SerializationType type;

  void AppendToBuffer(const char* data, size_t length) {
    if (bufferPos + length > MONITOR_BUFFER_SIZE) {
      Flush();
    }
    std::memcpy(buffer.data() + bufferPos, data, length);
    bufferPos += length;
  }

  void AppendCharToBuffer(char c) {
    if (bufferPos + 1 > MONITOR_BUFFER_SIZE ||
        c == '\n' && bufferPos + 200 > MONITOR_BUFFER_SIZE) {
      Flush();
    }
    buffer[bufferPos++] = c;
  }

  void AppendIntToBuffer(uint64_t value) {
    char tmp[20];
    char* p = tmp + 19;
    do {
      *p-- = '0' + (value % 10);
      value /= 10;
    } while (value > 0);
    AppendToBuffer(p + 1, tmp + 19 - p);
  }

 public:
  Serializer(SerializationType t)
      : buffer(MONITOR_BUFFER_SIZE), bufferPos(0), type(t) {}
  virtual ~Serializer() = default;

  virtual void Flush() = 0;

  void SerializeFunctionDef(const FunctionDef& func) {
    AppendIntToBuffer(func.id);
    AppendCharToBuffer('\t');
    AppendToBuffer(func.name.c_str(), func.name.length());
    if (func.is_constructor) {
      AppendToBuffer(".constructor", 12);
    }
    AppendCharToBuffer('\t');
    AppendIntToBuffer(func.line);
    AppendCharToBuffer('\t');
    AppendIntToBuffer(func.column);
    AppendCharToBuffer('\t');
    AppendIntToBuffer(func.file_index);
    AppendCharToBuffer('\t');
    AppendIntToBuffer(func.position_start);
    AppendCharToBuffer('\t');
    AppendIntToBuffer(func.position_end);
    AppendCharToBuffer('\t');
    AppendToBuffer(func.path.c_str(), func.path.length());
    AppendCharToBuffer('\t');
    AppendIntToBuffer(func.timestamp_ms);
    AppendCharToBuffer('\n');
  }

  void SerializeFunctionCall(const FunctionCall& call) {
    if (call.caller == UINT32_MAX) {
      AppendIntToBuffer(-1);
    } else {
      AppendIntToBuffer(call.caller);
    }
    AppendCharToBuffer('\t');
    if (call.callee == UINT32_MAX) {
      AppendIntToBuffer(-1);
    } else {
      AppendIntToBuffer(call.callee);
    }
    AppendCharToBuffer('\t');
    AppendIntToBuffer(call.timestamp_ms);
    AppendCharToBuffer('\n');
  }
};

class FileSerializer : public Serializer {
 private:
  std::ofstream outFile;

 public:
  FileSerializer(const std::string& filename)
      : Serializer(SerializationType::FILE) {
    outFile.open(filename, std::ios::binary);
    outFile.rdbuf()->pubsetbuf(nullptr, 0);  // Disable stream buffering
  }

  ~FileSerializer() override {
    Flush();
    if (outFile.is_open()) {
      outFile.close();
    }
  }

  void Flush() override {
    if (bufferPos > 0) {
      outFile.write(buffer.data(), bufferPos);
      bufferPos = 0;
    }
  }
};

class ApiSerializer : public Serializer {
 private:
  std::string endpoint;
  std::string host;
  std::string port;
  std::string target;
  std::mutex httpMutex;

 public:
  ApiSerializer(const std::string& url)
      : Serializer(SerializationType::API), endpoint(url) {
    ParseUrl(url);
  }

  ~ApiSerializer() override { Flush(); }

  void ParseUrl(const std::string& url) {
    size_t protocol_end = url.find("://");
    size_t host_start =
        (protocol_end == std::string::npos) ? 0 : protocol_end + 3;
    size_t host_end = url.find(':', host_start);
    if (host_end == std::string::npos) {
      host_end = url.find('/', host_start);
    }

    host = url.substr(host_start, host_end - host_start);

    size_t port_start = host_end;
    if (url[port_start] == ':') {
      ++port_start;
      size_t port_end = url.find('/', port_start);
      port = url.substr(port_start, port_end - port_start);
    } else {
      port = "80";
    }

    size_t target_start = url.find('/', host_end);
    if (target_start != std::string::npos) {
      target = url.substr(target_start);
    } else {
      target = "/";
    }
  }

  void Flush() override {
    if (bufferPos > 0) {
      SendHttpRequest();
      bufferPos = 0;
    }
  }

  void SendHttpRequest() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
      std::cerr << "Error opening socket" << std::endl;
      return;
    }

    struct hostent* server = gethostbyname(host.c_str());
    if (server == nullptr) {
      std::cerr << "Error, no such host" << std::endl;
      close(sockfd);
      return;
    }

    struct sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    std::memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(std::stoi(port));

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
      std::cerr << "Error connecting to server" << std::endl;
      close(sockfd);
      return;
    }

    std::string request;
    request.reserve(256 +
                    bufferPos);  // Reserve space for headers + buffer content

    request.append("POST ")
        .append(target)
        .append(" HTTP/1.1\r\nHost: ")
        .append(host)
        .append("\r\nUser-Agent: ApiSerializer\r\n")
        .append("Content-Type: text/plain\r\n")
        .append("Content-Length: ")
        .append(std::to_string(bufferPos))
        .append("\r\nConnection: close\r\n\r\n");

    // Append buffer content until bufferPos
    request.append(buffer.data(), bufferPos);

    if (send(sockfd, request.c_str(), request.length(), 0) < 0) {
      std::cerr << "Error sending request to server" << std::endl;
      close(sockfd);
      return;
    }

    char res_buffer[4096];
    std::string response;
    int bytes_received;
    while ((bytes_received =
                recv(sockfd, res_buffer, sizeof(res_buffer) - 1, 0)) > 0) {
      res_buffer[bytes_received] = '\0';
      response += res_buffer;
    }

    close(sockfd);
  }
};

class MonitorBase {
 protected:
  std::unique_ptr<Serializer> serializer;
  std::atomic<bool> is_init;
  std::mutex init_mutex;

 public:
  MonitorBase() : is_init(false) {}
  virtual ~MonitorBase() = default;

  bool IsInitialized() { return is_init.load(); }

  void Flush() {
    if (serializer) {
      serializer->Flush();
    }
  }
};

struct FunctionPosition {
  int start;
  int end;
  int line;
  int column;
};

class BytecodeMonitor : public MonitorBase {
 private:
  uint32_t global_func_id;
  bool trace_all;

  bool _isToTrace(std::string path) {
    return trace_all ||
           (path.find("node:") != 0 || path.find("node:timers") == 0) &&
               path.find("/ts-node/") == -1 &&
               path.find("/typescript/") == -1 && path.find("/npm/") == -1 &&
               path.find("/source-map-support/") == -1;
  }

  FunctionPosition _getPosition(
      v8::internal::Handle<v8::internal::Script> script,
      v8::internal::FunctionLiteral* literal) {
    v8::internal::Script::PositionInfo pos =
        v8::internal::Script::PositionInfo();
    int position = literal->function_token_position();
    if (!script.is_null()) {
      if (position == v8::internal::kNoSourcePosition) {
        position = literal->position();
      }
      if (position == v8::internal::kNoSourcePosition) {
        position = literal->start_position();
      }
      if (position != v8::internal::kNoSourcePosition) {
        v8::internal::Script::GetPositionInfo(script, position, &pos,
                                              v8::internal::Script::NO_OFFSET);
      }
    }
    return {position, literal->end_position(), pos.line + 1, pos.column + 1};
  }

 public:
  std::unordered_map<std::string, uint32_t> func2id;
  bool stack_collect;
  uint32_t stack_depth;

  BytecodeMonitor()
      : global_func_id(0),
        stack_depth(100),
        trace_all(false),
        stack_collect(false) {}

  void Initialize() {
    std::lock_guard<std::mutex> lock(init_mutex);
    if (!is_init.exchange(true)) {
      auto trace_depth_env = std::getenv("TRACE_DEPTH");
      if (trace_depth_env) {
        stack_depth = static_cast<uint32_t>(std::stoi(trace_depth_env));
      }
      if (stack_depth > 0) {
        stack_collect = true;
      }
      trace_all = std::getenv("TRACE_ALL") ? true : false;
      auto trace_entrypoint = std::getenv("TRACE_ENTRYPOINT");
      if (trace_entrypoint) {
        serializer = std::make_unique<ApiSerializer>(trace_entrypoint);
      } else {
        serializer = std::make_unique<FileSerializer>(
            "func_" + std::to_string(getpid()) + ".tsv");
      }
    }
  }

  uint32_t TraceReturnFunctionCreation(
      v8::internal::Handle<v8::internal::Script> script,
      v8::internal::FunctionLiteral* literal) {
    if (!is_init.load() || !stack_collect) return UINT32_MAX;

    if (!script->name().IsString()) return UINT32_MAX;
    const std::string path =
        v8::internal::String::cast(script->name())
            .ToCString(v8::internal::DISALLOW_NULLS,
                       v8::internal::ROBUST_STRING_TRAVERSAL)
            .get();
    if (!trace_all && !_isToTrace(path)) return UINT32_MAX;

    const int file_index = literal->function_literal_id();
    const std::string function_key = std::to_string(file_index) + path;

    if (file_index == 0) {
      return UINT32_MAX;
    }

    if (func2id.find(function_key) != func2id.end()) {
      return func2id[function_key];
    }
    return UINT32_MAX;
  }

  uint32_t TraceFunctionCreation(
      v8::internal::Handle<v8::internal::Script> script,
      v8::internal::FunctionLiteral* literal, bool is_constructor) {
    if (!script->name().IsString()) return UINT32_MAX;

    const std::string path =
        v8::internal::String::cast(script->name())
            .ToCString(v8::internal::DISALLOW_NULLS,
                       v8::internal::ROBUST_STRING_TRAVERSAL)
            .get();

    if (!IsInitialized()) {
      Initialize();
    }

    if (!trace_all && !_isToTrace(path)) return UINT32_MAX;

    const int file_index = literal->function_literal_id();
    const std::string function_key = std::to_string(file_index) + path;

    if (file_index == 0) {
      return UINT32_MAX;
    }

    if (func2id.find(function_key) != func2id.end()) {
      return func2id[function_key];
    }

    const uint32_t func_id = global_func_id++;
    func2id[function_key] = func_id;
    const auto pos = _getPosition(script, literal);
    const std::string name = literal->GetDebugName().get();

    // Get current time in milliseconds
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto epoch = now_ms.time_since_epoch();
    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();

    const FunctionDef
        func = {func_id,  file_index,  pos.start, pos.end,
                pos.line, pos.column,  name,      is_constructor,
                path,     milliseconds};  // Assign the timestamp here
    serializer->SerializeFunctionDef(func);
    return func_id;
  }
};

class RuntimeMonitor : public MonitorBase {
 private:
  std::unordered_set<FunctionCall, FunctionCallHash> known_calls;
  std::unordered_map<uintptr_t, uint32_t> address_to_func;
  std::unordered_map<uintptr_t, uint32_t> callback_to_func;
  uint32_t collect_stack_depth;
  bool collect_stack;

 public:
  RuntimeMonitor() : collect_stack_depth(0), collect_stack(false) {}

  void Initialize(uint32_t stack_depth) {
    std::lock_guard<std::mutex> lock(init_mutex);
    if (!is_init.exchange(true)) {
      collect_stack_depth = stack_depth;
      if (collect_stack_depth > 1) {
        collect_stack = true;
      }
      if (std::getenv("TRACE_ENTRYPOINT")) {
        serializer =
            std::make_unique<ApiSerializer>(std::getenv("TRACE_ENTRYPOINT"));
      } else {
        serializer = std::make_unique<FileSerializer>(
            "cg_" + std::to_string(getpid()) + ".tsv");
      }
    }
  }

  /**
   * Trace a function call. If the callee is not known, it will be added to the
   * set. If the callee is known and the caller is known, the call will be
   * serialized.
   */
  void TraceEnter(uint32_t callee, v8::internal::Isolate* isolate) {
    if (!is_init.load() || callee == UINT32_MAX) {
      return;
    }
    // Get current time in milliseconds
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto epoch = now_ms.time_since_epoch();
    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();

    if (!collect_stack) {
      serializer->SerializeFunctionCall({UINT32_MAX, callee, milliseconds});
      return;
    }
    if (collect_stack) {
      std::vector<uintptr_t> callbacks;
      uint32_t caller = UINT32_MAX;

      v8::internal::JavaScriptStackFrameIterator it(isolate);
      int level = 0;
      while (!it.done()) {
        auto frame = it.frame();
        if (frame->is_java_script()) {
          const v8::internal::JSFunction func = frame->function();
          const auto func_address = func.shared().address();
          if (level == 0) {
            address_to_func[func_address] = callee;
            const int length = frame->ComputeParametersCount();
            for (int i = 0; i < length; ++i) {
              auto param = frame->GetParameter(i);
              if (param.IsJSFunction()) {
                callbacks.push_back(
                    v8::internal::JSFunction::cast(param).shared().address());
              }
            }
            // the cb is colled, add edge between the cb and the caller
            if (callback_to_func.find(func_address) != callback_to_func.end()) {
              serializer->SerializeFunctionCall(
                  {callback_to_func[func_address], callee, milliseconds});
            }
          } else if (level >= 1) {
            if (address_to_func.find(func_address) != address_to_func.end()) {
              caller = address_to_func[func_address];
              break;
            }
          }
        }
        ++level;
        it.Advance();
      }
      if (caller == UINT32_MAX) {
        return;
      }
      FunctionCall call{caller, callee, milliseconds};
      if (known_calls.insert(call).second) {
        serializer->SerializeFunctionCall(call);
      }
      for (auto& c : callbacks) {
        callback_to_func[c] = call.caller;
      }
    }
  }

  /**
   * Trace a function exit. If the callee is known, the call will be serialized.
   */
  void TraceExit(uint32_t funcId) {
    if (!is_init.load() || collect_stack) {
      return;
    }
    serializer->SerializeFunctionCall({funcId, UINT32_MAX});
  }
};

// Global instances
extern BytecodeMonitor g_bytecode_monitor;
extern RuntimeMonitor g_runtime_monitor;

#endif  // ENDOR_MONITOR_H_