#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bridge.hpp"
#include "game.hpp"

namespace {
using json = pvz::json;
constexpr std::size_t kMaxMessage = 1024 * 1024;
constexpr std::size_t kQueueLimit = 64;
constexpr auto kQueueDeadline = std::chrono::seconds(2);
constexpr auto kIoDeadline = std::chrono::seconds(5);
constexpr auto kConnectDeadline = std::chrono::seconds(30);

enum class RequestState { queued, running, complete, canceled };
struct PendingRequest {
    std::mutex mutex;
    std::condition_variable changed;
    RequestState state = RequestState::queued;
    std::chrono::steady_clock::time_point deadline;
    json id;
    std::string operation;
    json arguments = json::object();
    json response;
};

std::mutex queue_mutex;
std::vector<std::shared_ptr<PendingRequest>> queue;
std::atomic<bool> initialized{false};
std::atomic<bool> stopping{false};
std::atomic<bool> detached{false};
std::string init_error;
DWORD process_id = 0;

json make_error(const json& id, const char* code, const std::string& message) {
    return json{{"id", id}, {"ok", false}, {"error", {{"code", code}, {"message", message}}}};
}

std::wstring pipe_name() {
    return L"\\\\.\\pipe\\pvz-ai-" + std::to_wstring(process_id);
}

bool make_pipe_security(SECURITY_ATTRIBUTES& attributes, PSECURITY_DESCRIPTOR& descriptor) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<unsigned char> token_data(bytes);
    const bool got_user = bytes != 0 && GetTokenInformation(token, TokenUser, token_data.data(), bytes, &bytes);
    CloseHandle(token);
    if (!got_user) return false;
    const auto* user = reinterpret_cast<const TOKEN_USER*>(token_data.data());
    LPWSTR sid_text = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid_text)) return false;
    const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid_text) + L")";
    LocalFree(sid_text);
    descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) return false;
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    return true;
}

bool read_line(HANDLE pipe, std::string& line) {
    line.clear();
    const auto deadline = std::chrono::steady_clock::now() + kIoDeadline;
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr)) {
            const DWORD error = GetLastError();
            if ((error == ERROR_NO_DATA || error == ERROR_PIPE_LISTENING) && std::chrono::steady_clock::now() < deadline) {
                Sleep(5);
                continue;
            }
            return false;
        }
        if (read == 0) {
            if (std::chrono::steady_clock::now() < deadline) { Sleep(5); continue; }
            return false;
        }
        const char* begin = buffer;
        const char* end = buffer + read;
        const char* newline = std::find(begin, end, '\n');
        const std::size_t take = static_cast<std::size_t>((newline == end ? end : newline) - buffer);
        if (line.size() + take > kMaxMessage) return false;
        line.append(buffer, take);
        if (newline != end) {
            if (newline != end - 1) return false; // Exactly one request per connection.
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
    }
}

bool write_all(HANDLE pipe, const std::string& data) {
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + kIoDeadline;
    while (offset < data.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - offset, MAXDWORD));
        if (!WriteFile(pipe, data.data() + offset, chunk, &written, nullptr)) {
            const DWORD error = GetLastError();
            if ((error == ERROR_NO_DATA || error == ERROR_PIPE_LISTENING) && std::chrono::steady_clock::now() < deadline) {
                Sleep(5);
                continue;
            }
            return false;
        }
        if (written == 0) {
            if (std::chrono::steady_clock::now() < deadline) { Sleep(5); continue; }
            return false;
        }
        offset += written;
    }
    return true;
}

json hello_when_unavailable() {
    json result = {
        {"process", {{"pid", process_id}, {"version", "1.2.0.1096"}}},
        {"version", "1.2.0.1096"}, {"capabilities", json::array()},
        {"ready", false}, {"initialization_error", init_error.empty() ? "Game hook is not available." : init_error}
    };
    return result;
}

json submit_request(const json& id, const std::string& operation, const json& args) {
    if (operation == "hello" && (!initialized.load() || detached.load())) {
        if (detached.load()) return make_error(id, "detached", "This bridge instance was detached; attach a new versioned bridge DLL to resume control.");
        return json{{"id", id}, {"ok", true}, {"result", hello_when_unavailable()}};
    }
    if (!initialized.load() || detached.load() || stopping.load()) {
        return make_error(id, "not_ready", init_error.empty() ? "Game control is not available." : init_error);
    }
    auto request = std::make_shared<PendingRequest>();
    request->deadline = std::chrono::steady_clock::now() + kQueueDeadline;
    request->id = id;
    request->operation = operation;
    request->arguments = args;
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (queue.size() >= kQueueLimit) return make_error(id, "busy", "The game-thread request queue is full.");
        queue.push_back(request);
    }
    std::unique_lock<std::mutex> lock(request->mutex);
    if (!request->changed.wait_until(lock, request->deadline, [&] { return request->state == RequestState::complete || request->state == RequestState::canceled; })) {
        // A request that began execution is allowed to finish. Returning a timeout then
        // could make the caller retry an action that is still taking effect.
        if (request->state == RequestState::queued) {
            request->state = RequestState::canceled;
            return make_error(id, "timeout", "Request expired before the game thread could execute it.");
        }
        request->changed.wait(lock, [&] { return request->state == RequestState::complete; });
    }
    if (request->state == RequestState::canceled) return make_error(id, "timeout", "Request expired before the game thread could execute it.");
    return request->response;
}

bool wait_for_client_close(HANDLE pipe) {
    const auto deadline = std::chrono::steady_clock::now() + kIoDeadline;
    do {
        char unexpected = 0;
        DWORD read = 0;
        if (ReadFile(pipe, &unexpected, 1, &read, nullptr)) {
            if (read != 0) return false; // The protocol permits only one request per connection.
        } else {
            const DWORD error = GetLastError();
            if (error == ERROR_BROKEN_PIPE || error == ERROR_PIPE_NOT_CONNECTED) return true;
            if (error != ERROR_NO_DATA && error != ERROR_PIPE_LISTENING) return false;
        }
        Sleep(5);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

void serve_client(HANDLE pipe) {
    std::string line;
    if (!read_line(pipe, line)) return;
    json id = nullptr;
    json reply;
    bool detach_after_reply = false;
    try {
        const json request = json::parse(line);
        id = request.value("id", json(nullptr));
        if (!request.is_object() || !request.contains("op") || !request["op"].is_string()) {
            reply = make_error(id, "invalid_request", "Request must contain a string op.");
        } else {
            const std::string operation = request["op"].get<std::string>();
            const json args = request.value("args", json::object());
            if (!args.is_object()) reply = make_error(id, "invalid_request", "args must be a JSON object.");
            else if (operation == "detach" && !initialized.load()) {
                reply = json{{"id", id}, {"ok", true}, {"result", {
                    {"detached", true}, {"ready", false},
                    {"initialization_error", init_error.empty() ? "Game control was not initialized." : init_error}
                }}};
                detach_after_reply = true;
            } else {
                reply = submit_request(id, operation, args);
                detach_after_reply = operation == "detach" && reply.value("ok", false);
            }
        }
    } catch (const json::parse_error&) {
        reply = make_error(id, "invalid_json", "Request is not valid JSON.");
    } catch (const std::exception& error) {
        reply = make_error(id, "request_error", error.what());
    }
    std::string serialized = reply.dump();
    if (serialized.size() > kMaxMessage) {
        serialized = make_error(id, "response_too_large", "Bridge response exceeded the 1 MiB message limit.").dump();
    }
    serialized.push_back('\n');
    const bool written = write_all(pipe, serialized);
    // The pipe is nonblocking. Do not disconnect immediately after WriteFile:
    // accepted bytes may still be unread in the pipe buffer. The host uses one
    // request per connection and closes after reading the newline reply.
    if (written) wait_for_client_close(pipe);
    if (detach_after_reply) {
        stopping.store(true);
    }
}

void pipe_worker() {
    SECURITY_ATTRIBUTES attributes{};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!make_pipe_security(attributes, descriptor)) {
        init_error = "Could not create the current-user pipe access rule.";
        return;
    }
    const std::wstring name = pipe_name();
    while (!stopping.load()) {
        HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 64 * 1024, 64 * 1024, 0, &attributes);
        if (pipe == INVALID_HANDLE_VALUE) break;
        const auto deadline = std::chrono::steady_clock::now() + kConnectDeadline;
        BOOL connected = FALSE;
        while (!stopping.load() && std::chrono::steady_clock::now() < deadline) {
            if (ConnectNamedPipe(pipe, nullptr)) { connected = TRUE; break; }
            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) { connected = TRUE; break; }
            if (error != ERROR_PIPE_LISTENING) break;
            Sleep(10);
        }
        if (connected) serve_client(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    LocalFree(descriptor);
}

DWORD WINAPI worker_entry(void*) {
    process_id = GetCurrentProcessId();
    std::string error;
    try {
        if (!pvz::initialize(error)) init_error = error.empty() ? "Supported game initialization failed." : error;
        else initialized.store(true);
    } catch (const std::exception& exception) {
        init_error = std::string("Initialization failed: ") + exception.what();
    } catch (...) {
        init_error = "Initialization failed with an unknown native error.";
    }
    pipe_worker();
    return 0;
}
}

namespace bridge {
void process_pending() {
    const auto drain_started = std::chrono::steady_clock::now();
    std::size_t processed = 0;
    for (;;) {
        if (processed >= 8 || std::chrono::steady_clock::now() - drain_started >= std::chrono::milliseconds(2)) return;
        std::shared_ptr<PendingRequest> request;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (queue.empty() || detached.load()) return;
            request = queue.front();
            queue.erase(queue.begin());
        }
        {
            std::lock_guard<std::mutex> lock(request->mutex);
            if (request->state == RequestState::canceled || std::chrono::steady_clock::now() >= request->deadline) {
                request->state = RequestState::canceled;
                request->changed.notify_all();
                continue;
            }
            request->state = RequestState::running;
        }
        json response;
        try {
            const json result = pvz::dispatch(request->operation, request->arguments);
            response = json{{"id", request->id}, {"ok", true}, {"result", result}};
            if (request->operation == "detach") {
                detached.store(true);
                pvz::shutdown();
                std::lock_guard<std::mutex> lock(queue_mutex);
                for (const auto& queued : queue) {
                    std::lock_guard<std::mutex> queued_lock(queued->mutex);
                    queued->state = RequestState::canceled;
                    queued->changed.notify_all();
                }
                queue.clear();
            }
        } catch (const std::exception& exception) {
            response = make_error(request->id, "dispatch_error", exception.what());
        } catch (...) {
            response = make_error(request->id, "dispatch_error", "Game-thread dispatch failed with an unknown native error.");
        }
        {
            std::lock_guard<std::mutex> lock(request->mutex);
            request->response = std::move(response);
            request->state = RequestState::complete;
        }
        request->changed.notify_all();
        ++processed;
        if (detached.load()) return;
    }
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        HANDLE worker = CreateThread(nullptr, 0, worker_entry, nullptr, 0, nullptr);
        if (worker) CloseHandle(worker);
        else OutputDebugStringW(L"pvz_bridge: worker creation failed; bridge pipe was not started.\n");
    }
    // Deliberately do not tear down hooks or wait here: DllMain runs under loader lock.
    return TRUE;
}
