module;

#include <android/log.h>
#include <atomic>
#include <mutex>

export module thread;

import hook_helper;

#define LOG_TAG "LSPlant"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace lsplant::art {

    export class Thread {
        inline static Function<"_ZN3art6Thread14CurrentFromGdbEv", Thread *()> CurrentFromGdb_{}; // 使用默认构造函数初始化
        inline static std::atomic<bool> is_initialized{false}; // 线程安全检查
        inline static std::mutex init_mutex;

    public:
        static Thread *Current() {
            if (CurrentFromGdb_) [[likely]] {
                return CurrentFromGdb_();
            } else {
                LOGE("CurrentFromGdb_ is nullptr. Returning nullptr.");
                return nullptr;
            }
        }

        static bool Init(const HookHandler &handler) {
            if (is_initialized.load()) {
                return true;
            }

            std::lock_guard<std::mutex> const lock(init_mutex); // 确保线程安全
            if (!is_initialized.load()) {
                if (!handler.dlsym(CurrentFromGdb_)) [[unlikely]] {
                    LOGE("Failed to resolve symbol: CurrentFromGdb_");
                    return false;
                }
                is_initialized.store(true);
            }
            return true;
        }
    };

}  // namespace lsplant::art
