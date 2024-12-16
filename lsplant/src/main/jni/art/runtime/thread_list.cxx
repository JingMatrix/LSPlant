module;

#include <android/log.h>
#include <atomic>
#include <mutex>

export module thread_list;

import hook_helper;

#define LOG_TAG "LSPlant"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace lsplant::art::thread_list {

    export class ScopedSuspendAll {
        inline static MemberFunction<"_ZN3art16ScopedSuspendAllC2EPKcb", ScopedSuspendAll,
                void(const char *, bool)>
                constructor_{};
        inline static MemberFunction<"_ZN3art16ScopedSuspendAllD2Ev", ScopedSuspendAll, void()>
                destructor_{};

        inline static Function<"_ZN3art3Dbg9SuspendVMEv", void()> SuspendVM_{};
        inline static Function<"_ZN3art3Dbg8ResumeVMEv", void()> ResumeVM_{};

        inline static std::atomic<bool> is_initialized{false};
        inline static std::mutex init_mutex;

    public:
        ScopedSuspendAll(const char *cause, bool long_suspend) {
            if (constructor_) {
                constructor_(this, cause, long_suspend);
            } else if (SuspendVM_) {
                LOGE("ScopedSuspendAll: %s", "Fallback: using SuspendVM_");
                SuspendVM_();
            } else {
                LOGE("ScopedSuspendAll: %s", "Constructor failed: no valid method available.");
            }
        }

        ~ScopedSuspendAll() {
            if (destructor_) {
                destructor_(this);
            } else if (ResumeVM_) {
                LOGE("ScopedSuspendAll: %s", "Fallback: using ResumeVM_");
                ResumeVM_();
            } else {
                LOGE("ScopedSuspendAll: %s", "Destructor failed: no valid method available.");
            }
        }

        static bool Init(const HookHandler &handler) {
            if (is_initialized.load()) {
                return true;
            }

            std::lock_guard<std::mutex> const lock(init_mutex);
            if (!is_initialized.load()) {
                if (!handler.dlsym(constructor_) && !handler.dlsym(SuspendVM_)) [[unlikely]] {
                    LOGE("ScopedSuspendAll: %s", "Failed to resolve constructor_ or SuspendVM_");
                    return false;
                }

                if (!handler.dlsym(destructor_) && !handler.dlsym(ResumeVM_)) [[unlikely]] {
                    LOGE("ScopedSuspendAll: %s", "Failed to resolve destructor_ or ResumeVM_");
                    return false;
                }

                is_initialized.store(true);
            }

            return true;
        }
    };

}  // namespace lsplant::art::thread_list
