module;

#include <array>
#include <atomic>
#include <mutex>

#include "logging.hpp"

export module lsplant:runtime;

import :common;
import hook_helper;

namespace lsplant::art {

export class Runtime {
public:
    enum class RuntimeDebugState {
        // This doesn't support any debug features / method tracing. This is the expected state
        // usually.
        kNonJavaDebuggable,
        // This supports method tracing and a restricted set of debug features (for ex: redefinition
        // isn't supported). We transition to this state when method tracing has started or when the
        // debugger was attached and transition back to NonDebuggable once the tracing has stopped /
        // the debugger agent has detached..
        kJavaDebuggable,
        // The runtime was started as a debuggable runtime. This allows us to support the extended
        // set
        // of debug features (for ex: redefinition). We never transition out of this state.
        kJavaDebuggableAtInit
    };

private:
    inline static auto instance_ = "_ZN3art7Runtime9instance_E"_sym.as<Runtime *>;

    inline static auto SetJavaDebuggable_ =
        "_ZN3art7Runtime17SetJavaDebuggableEb"_sym.as<void (Runtime::*)(bool)>;

    inline static auto SetRuntimeDebugState_ =
        "_ZN3art7Runtime20SetRuntimeDebugStateENS0_17RuntimeDebugStateE"_sym
            .as<void (Runtime::*)(RuntimeDebugState)>;

    inline static size_t debug_state_offset = 0U;
    inline static std::once_flag debug_state_offset_flag;

    static void FindDebugStateOffset() {
        if (!SetRuntimeDebugState_) {
            LOGW("SetRuntimeDebugState_ symbol not available, cannot find debug_state_offset.");
            return;
        }

        static constexpr size_t kLargeEnoughSizeForRuntime = 4096;
        std::array<uint8_t, kLargeEnoughSizeForRuntime> buffer;
        static_assert(static_cast<int>(RuntimeDebugState::kJavaDebuggable) != 0);

        buffer.fill(uint8_t{0});
        auto *const fake_runtime = reinterpret_cast<Runtime *>(buffer.data());

        // Call the function on a fake object on the stack.
        SetRuntimeDebugState_(fake_runtime, RuntimeDebugState::kJavaDebuggable);

        // Find the location where the state was written.
        for (size_t i = 0; i < kLargeEnoughSizeForRuntime; ++i) {
            if (*reinterpret_cast<RuntimeDebugState *>(reinterpret_cast<uintptr_t>(fake_runtime) +
                                                       i) == RuntimeDebugState::kJavaDebuggable) {
                LOGD("Found debug_state at offset %zu", i);
                debug_state_offset = i;
                return;
            }
        }
        LOGE("Failed to find debug_state_offset.");
    }

public:
    inline static Runtime *Current() { return *instance_; }

    void SetJavaDebuggable(RuntimeDebugState value) {
        if (SetJavaDebuggable_) {
            SetJavaDebuggable_(this, value != RuntimeDebugState::kNonJavaDebuggable);
        } else {
            std::call_once(debug_state_offset_flag, FindDebugStateOffset);
            if (debug_state_offset > 0) {
                *reinterpret_cast<RuntimeDebugState *>(reinterpret_cast<uintptr_t>(*instance_) +
                                                       debug_state_offset) = value;
            }
        }
    }

    static bool Init(const HookHandler &handler) {
        if (!handler(instance_) || !*instance_) {
            LOGE("Failed to find Runtime::instance_");
            return false;
        }
        LOGD("runtime instance = %p", *instance_);

        if (GetAndroidApiLevel() >= __ANDROID_API_O__) {
            if (!handler(SetJavaDebuggable_, SetRuntimeDebugState_)) {
                return false;
            };
        }

        return true;
    }
};

export struct JavaDebuggableGuard {
    JavaDebuggableGuard() {
        while (true) {
            size_t expected = 0;
            if (count.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
                Runtime::Current()->SetJavaDebuggable(
                    Runtime::RuntimeDebugState::kJavaDebuggableAtInit);
                count.fetch_add(1, std::memory_order_release);
                count.notify_all();
                break;
            }
            if (expected == 1) {
                count.wait(expected, std::memory_order_acquire);
                continue;
            }
            if (count.compare_exchange_strong(expected, expected + 1, std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
                break;
            }
        }
    }

    ~JavaDebuggableGuard() {
        while (true) {
            size_t expected = 2;
            if (count.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
                Runtime::Current()->SetJavaDebuggable(
                    Runtime::RuntimeDebugState::kNonJavaDebuggable);
                count.fetch_sub(1, std::memory_order_release);
                count.notify_all();
                break;
            }
            if (expected == 1) {
                count.wait(expected, std::memory_order_acquire);
                continue;
            }
            if (count.compare_exchange_strong(expected, expected - 1, std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
                break;
            }
        }
    }

private:
    inline static std::atomic_size_t count{0};
    static_assert(std::atomic_size_t::is_always_lock_free, "Unsupported architecture");
    static_assert(std::is_same_v<std::atomic_size_t::value_type, size_t>,
                  "Unsupported architecture");
};
}  // namespace lsplant::art
