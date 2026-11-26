// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <cameraunlock/unreal/ue_reflection.h>

// Calling a reflected UFunction through UObject::ProcessEvent, with its
// parameter block laid out from the function's own properties.
namespace cameraunlock::unreal {

class ScriptFunction {
public:
    ScriptFunction() = default;

    // The function at `path` ("/Script/Engine.GameplayStatics:IsGamePaused").
    // Throws when it is not loaded or has no parameter block.
    ScriptFunction(const Reflection& refl, std::uintptr_t processEvent, const std::string& path);

    const std::string& Path() const { return path_; }
    // Throws when the function has no such parameter. A caller looks up every
    // parameter it will set or read where it is built, so a build that names or
    // sizes one differently fails there and not inside a hook.
    const Field& Param(std::string_view name) const;
    // The same, and throws unless the parameter is `size` bytes.
    const Field& Param(std::string_view name, std::size_t size) const;

    // The inline block's 16-byte alignment pads the class, which is intended.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    class Frame {
    public:
        explicit Frame(const ScriptFunction& fn) : fn_(fn) {
            const auto size = static_cast<std::size_t>(fn.size_);
            if (size > kInlineBytes) {
                heap_.assign(size, 0);
                params_ = heap_.data();
            } else {
                std::memset(inline_, 0, size);
                params_ = inline_;
            }
        }
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        template <class T>
        void Set(std::string_view param, const T& value) { SetBytes(param, &value, sizeof(T)); }
        void SetBytes(std::string_view param, const void* data, std::size_t size) {
            std::memcpy(params_ + fn_.Param(param, size).offset, data, size);
        }
        template <class T>
        T Get(std::string_view param) const {
            T value{};
            std::memcpy(&value, params_ + fn_.Param(param).offset, sizeof(T));
            return value;
        }
        // The parameter's bytes, for reading a struct's fields by their own offsets.
        const std::uint8_t* Bytes(std::string_view param) const { return params_ + fn_.Param(param).offset; }
        void Call(std::uintptr_t object);

    private:
        // Frames are made several times a frame in hooks, so a block this size or
        // smaller lives on the stack. Each frame has its own block: a call can run
        // script that calls the same function again.
        static constexpr std::size_t kInlineBytes = 512;
        const ScriptFunction& fn_;
        alignas(16) std::uint8_t inline_[kInlineBytes];
        std::vector<std::uint8_t> heap_;
        std::uint8_t* params_ = nullptr;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

private:
    std::string path_;
    std::uintptr_t function_ = 0;
    std::uintptr_t processEvent_ = 0;
    std::int32_t size_ = 0;
    std::vector<Field> params_;
};

}  // namespace cameraunlock::unreal
