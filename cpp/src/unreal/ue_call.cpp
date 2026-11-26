// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/unreal/ue_call.h>

#include <cstring>
#include <stdexcept>

namespace cameraunlock::unreal {

ScriptFunction::ScriptFunction(const Reflection& refl, std::uintptr_t processEvent, const std::string& path)
    : path_(path), processEvent_(processEvent) {
    function_ = refl.Objs().FindPath(path);
    if (!function_) throw std::runtime_error(path + " is not loaded");
    size_ = refl.StructSize(function_);
    params_ = refl.Properties(function_);
    if (size_ <= 0 || params_.empty()) throw std::runtime_error(path + " has no parameters");
    for (const Field& f : params_)
        if (f.offset < 0 || f.offset + f.size > size_)
            throw std::runtime_error(path + " parameter " + f.name + " lies outside its block");
}

const Field& ScriptFunction::Param(std::string_view name) const {
    // FNames compare without case and print as the name was first spelt
    // anywhere in the game, so a parameter's printed case is not the source's.
    for (const Field& f : params_)
        if (f.name.size() == name.size() && _strnicmp(f.name.data(), name.data(), name.size()) == 0) return f;
    throw std::logic_error(path_ + " has no parameter " + std::string(name));
}

const Field& ScriptFunction::Param(std::string_view name, std::size_t size) const {
    const Field& f = Param(name);
    if (size != static_cast<std::size_t>(f.size))
        throw std::logic_error(path_ + " " + std::string(name) + " is " + std::to_string(f.size) + " bytes, not " + std::to_string(size));
    return f;
}

void ScriptFunction::Frame::Call(std::uintptr_t object) {
    using ProcessEvent = void(__fastcall*)(std::uintptr_t, std::uintptr_t, void*);
    reinterpret_cast<ProcessEvent>(fn_.processEvent_)(object, fn_.function_, params_);
}

}  // namespace cameraunlock::unreal
