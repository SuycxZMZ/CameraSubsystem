/**
 * @file signal_handler.cpp
 * @brief 信号处理工具类实现
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#include "camera_subsystem/utils/signal_handler.h"

#include <iostream>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <mutex>

namespace camera_subsystem {
namespace utils {

// 静态成员初始化
SignalHandler* SignalHandler::s_instance_ = nullptr;
std::mutex SignalHandler::s_mutex_;

SignalHandler::SignalHandler()
    : should_stop_(false)
    , captured_signal_(0)
    , initialized_(false)
{
}

SignalHandler::~SignalHandler()
{
    if (initialized_)
    {
        RestoreDefaultHandler(SIGINT);
        RestoreDefaultHandler(SIGTERM);
    }
}

SignalHandler& SignalHandler::GetInstance()
{
    std::lock_guard<std::mutex> lock(s_mutex_);
    if (s_instance_ == nullptr)
    {
        s_instance_ = new SignalHandler();
    }
    return *s_instance_;
}

bool SignalHandler::Initialize()
{
    std::lock_guard<std::mutex> lock(s_mutex_);

    if (initialized_)
    {
        std::cerr << "SignalHandler already initialized" << std::endl;
        return false;
    }

    SetDefaultStopCallback();

    if (!RegisterSignal(SIGINT, [this](int signal) { HandleSignalImpl(signal); }))
    {
        std::cerr << "Failed to register SIGINT handler" << std::endl;
        return false;
    }

    if (!RegisterSignal(SIGTERM, [this](int signal) { HandleSignalImpl(signal); }))
    {
        std::cerr << "Failed to register SIGTERM handler" << std::endl;
        RestoreDefaultHandler(SIGINT);
        return false;
    }

    initialized_ = true;
    return true;
}

bool SignalHandler::RegisterSignal(int signal, SignalCallback callback)
{
    if (!IsValidSignal(signal))
    {
        std::cerr << "Invalid signal: " << signal << std::endl;
        return false;
    }

    struct sigaction sa;
    sa.sa_handler = HandleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(signal, &sa, nullptr) < 0)
    {
        std::cerr << "Failed to register signal handler for " << GetSignalName(signal) << ": "
                  << strerror(errno) << std::endl;
        return false;
    }

    callback_ = std::move(callback);
    return true;
}

bool SignalHandler::ShouldStop() const
{
    return should_stop_.load();
}

void SignalHandler::Reset()
{
    should_stop_.store(false);
    captured_signal_.store(0);
}

int SignalHandler::GetCapturedSignal() const
{
    return captured_signal_.load();
}

const char* SignalHandler::GetSignalName(int signal)
{
    switch (signal)
    {
        case SIGINT:
            return "SIGINT";
        case SIGTERM:
            return "SIGTERM";
        case SIGKILL:
            return "SIGKILL";
        case SIGHUP:
            return "SIGHUP";
        case SIGQUIT:
            return "SIGQUIT";
        case SIGSEGV:
            return "SIGSEGV";
        case SIGABRT:
            return "SIGABRT";
        case SIGFPE:
            return "SIGFPE";
        case SIGBUS:
            return "SIGBUS";
        case SIGPIPE:
            return "SIGPIPE";
        case SIGALRM:
            return "SIGALRM";
        case SIGUSR1:
            return "SIGUSR1";
        case SIGUSR2:
            return "SIGUSR2";
        case SIGCHLD:
            return "SIGCHLD";
        case SIGCONT:
            return "SIGCONT";
        case SIGSTOP:
            return "SIGSTOP";
        case SIGTSTP:
            return "SIGTSTP";
        default:
            return "UNKNOWN";
    }
}

void SignalHandler::SetDefaultStopCallback()
{
    callback_ = [this](int signal) { DefaultStopCallback(signal); };
}

void SignalHandler::HandleSignal(int signal)
{
    if (s_instance_ != nullptr)
    {
        s_instance_->HandleSignalImpl(signal);
    }
}

void SignalHandler::HandleSignalImpl(int signal)
{
    captured_signal_.store(signal);
    PrintSignalInfo(signal);

    if (callback_)
    {
        callback_(signal);
    }
}

void SignalHandler::DefaultStopCallback(int signal)
{
    (void)signal;
    should_stop_.store(true);
}

void SignalHandler::PrintSignalInfo(int signal) const
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "Received signal: " << GetSignalName(signal) << " (" << signal << ")"
              << std::endl;
    std::cout << "========================================" << std::endl;
}

void SignalHandler::RestoreDefaultHandler(int signal)
{
    struct sigaction sa;
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(signal, &sa, nullptr) < 0)
    {
        std::cerr << "Failed to restore default handler for " << GetSignalName(signal) << ": "
                  << strerror(errno) << std::endl;
    }
}

bool SignalHandler::IsValidSignal(int signal) const
{
    switch (signal)
    {
        case SIGHUP:
        case SIGINT:
        case SIGQUIT:
        case SIGILL:
        case SIGTRAP:
        case SIGABRT:
        case SIGBUS:
        case SIGFPE:
        case SIGKILL:
        case SIGUSR1:
        case SIGSEGV:
        case SIGUSR2:
        case SIGPIPE:
        case SIGALRM:
        case SIGTERM:
#ifdef SIGSTKFLT
        case SIGSTKFLT:
#endif
        case SIGCHLD:
        case SIGCONT:
        case SIGSTOP:
        case SIGTSTP:
        case SIGTTIN:
        case SIGTTOU:
        case SIGURG:
        case SIGXCPU:
        case SIGXFSZ:
        case SIGVTALRM:
        case SIGPROF:
        case SIGWINCH:
        case SIGIO:
        case SIGPWR:
#ifdef SIGSYS
        case SIGSYS:
#endif
            return true;
        default:
            return false;
    }
}

} // namespace utils
} // namespace camera_subsystem
