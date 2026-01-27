/**
 * @file platform_thread.h
 * @brief 平台线程封装
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#ifndef CAMERA_SUBSYSTEM_PLATFORM_PLATFORM_THREAD_H
#define CAMERA_SUBSYSTEM_PLATFORM_PLATFORM_THREAD_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace camera_subsystem {
namespace platform {

/**
 * @brief 平台线程类
 *
 * 封装线程操作,提供统一的线程接口。
 * 支持线程优先级设置、CPU 亲和性设置等功能。
 */
class PlatformThread
{
public:
    /**
     * @brief 线程执行函数类型
     */
    using ThreadFunc = std::function<void()>;

    /**
     * @brief 构造函数
     * @param name 线程名称 (用于调试)
     * @param func 线程执行函数
     */
    PlatformThread(const std::string& name, ThreadFunc func);

    /**
     * @brief 析构函数
     *
     * @note 如果线程仍在运行,会自动调用 Join()
     */
    ~PlatformThread();

    /**
     * @brief 启动线程
     * @return 成功返回 true,失败返回 false
     *
     * @note 该函数不是线程安全的,不能在多线程中同时调用
     */
    bool Start();

    /**
     * @brief 等待线程结束
     *
     * @note 该函数会阻塞调用线程,直到目标线程结束
     * @note 如果线程已经结束,该函数会立即返回
     */
    void Join();

    /**
     * @brief 分离线程
     *
     * @note 分离后不能再调用 Join()
     * @note 分离的线程会在结束时自动释放资源
     */
    void Detach();

    /**
     * @brief 检查线程是否正在运行
     * @return true 表示正在运行,false 表示未运行
     *
     * @note 该函数是线程安全的
     */
    bool IsRunning() const;

    /**
     * @brief 获取线程 ID
     * @return 线程 ID,如果线程未启动返回默认值
     *
     * @note 该函数是线程安全的
     */
    std::thread::id GetThreadId() const;

    /**
     * @brief 获取线程名称
     * @return 线程名称
     *
     * @note 该函数是线程安全的
     */
    const std::string& GetThreadName() const;

    /**
     * @brief 设置线程优先级
     * @param priority 优先级 (-20 到 19, 数值越小优先级越高)
     * @return 成功返回 true,失败返回 false
     *
     * @note 该函数是线程安全的
     * @note 需要 root 权限才能设置实时优先级
     */
    bool SetPriority(int priority);

    /**
     * @brief 设置线程 CPU 亲和性
     * @param cpu_ids CPU 核心 ID 列表
     * @return 成功返回 true,失败返回 false
     *
     * @note 该函数是线程安全的
     */
    bool SetCpuAffinity(const std::vector<int>& cpu_ids);

    /**
     * @brief 请求线程停止
     *
     * @note 该函数只是设置停止标志,线程需要自行检查并退出
     * @note 该函数是线程安全的
     */
    void RequestStop();

    /**
     * @brief 检查是否请求停止
     * @return true 表示请求停止,false 表示未请求
     *
     * @note 该函数是线程安全的
     */
    bool IsStopRequested() const;

private:
    /**
     * @brief 线程入口函数
     */
    void ThreadEntry();

    std::string             thread_name_;
    ThreadFunc              thread_func_;
    std::unique_ptr<std::thread> native_thread_;
    std::atomic<bool>       is_running_;
    std::atomic<bool>       should_stop_;
    std::atomic<bool>       is_detached_;
};

} // namespace platform
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_PLATFORM_PLATFORM_THREAD_H
