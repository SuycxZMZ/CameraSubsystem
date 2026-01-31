/**
 * @file signal_handler.h
 * @brief 信号处理工具类
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#ifndef CAMERA_SUBSYSTEM_UTILS_SIGNAL_HANDLER_H
#define CAMERA_SUBSYSTEM_UTILS_SIGNAL_HANDLER_H

#include <atomic>
#include <csignal>
#include <functional>
#include <mutex>

namespace camera_subsystem {
namespace utils {

/**
 * @brief 信号处理工具类
 *
 * 提供统一的信号处理机制,支持多种信号的捕获和自定义回调处理。
 * 采用单例模式,确保全局只有一个信号处理器。
 */
class SignalHandler
{
public:
    /**
     * @brief 信号回调函数类型
     * @param signal 信号编号
     */
    using SignalCallback = std::function<void(int signal)>;

    /**
     * @brief 获取 SignalHandler 单例
     * @return SignalHandler 实例引用
     */
    static SignalHandler& GetInstance();

    /**
     * @brief 初始化信号处理器
     *
     * 注册 SIGINT 和 SIGTERM 信号的处理函数。
     * 调用此函数后,可以通过 ShouldStop() 来检查是否收到停止信号。
     *
     * @return 成功返回 true,失败返回 false
     */
    bool Initialize();

    /**
     * * @brief 注册信号处理器
     *
     * @param signal 信号编号(如 SIGINT, SIGTERM)
     * @param callback 信号回调函数
     * @return 成功返回 true,失败返回 false
     */
    bool RegisterSignal(int signal, SignalCallback callback);

    /**
     * @brief 检查是否应该停止运行
     * @return true 表示收到停止信号,false 表示继续运行
     */
    bool ShouldStop() const;

    /**
     * @brief 重置停止标志
     */
    void Reset();

    /**
     * @brief 获取捕获到的信号编号
     * @return 捕获到的信号编号,0 表示未捕获
     */
    int GetCapturedSignal() const;

    /**
     * @brief 获取信号名称
     * @param signal 信号编号
     * @return 信号名称字符串
     */
    static const char* GetSignalName(int signal);

    /**
     * @brief 设置默认停止信号处理回调
     *
     * 默认回调会打印信号信息并设置停止标志。
     */
    void SetDefaultStopCallback();

private:
    /**
     * @brief 构造函数
     */
    SignalHandler();

    /**
     * @brief 析构函数
     */
    ~SignalHandler();

    /**
     * @brief 禁止拷贝构造
     */
    SignalHandler(const SignalHandler&) = delete;

    /**
     * @brief 禁止拷贝赋值
     */
    SignalHandler& operator=(const SignalHandler&) = delete;

    /**
     * @brief 信号处理函数(静态成员函数)
     * @param signal 信号编号
     */
    static void HandleSignal(int signal);

    /**
     * @brief 执行信号处理回调
     * @param signal 信号编号
     */
    void HandleSignalImpl(int signal);

    /**
     * @brief 默认的信号处理回调
     * @param signal 信号编号
     */
    void DefaultStopCallback(int signal);

    /**
     * @brief 打印信号信息
     * @param signal 信号编号
     */
    void PrintSignalInfo(int signal) const;

    /**
     * @brief 恢复默认信号处理
     * @param signal 信号编号
     */
    void RestoreDefaultHandler(int signal);

    /**
     * @brief 检查信号是否有效
     * @param signal 信号编号
     * @return true 表示有效,false 表示无效
     */
    bool IsValidSignal(int signal) const;

    // 成员变量
    static SignalHandler* s_instance_;
    static std::mutex s_mutex_;
    std::atomic<bool> should_stop_;
    std::atomic<int> captured_signal_;
    SignalCallback callback_;
    bool initialized_;
};

} // namespace utils
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_UTILS_SIGNAL_HANDLER_H
