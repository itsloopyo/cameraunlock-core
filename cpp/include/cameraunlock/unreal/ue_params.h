// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <cameraunlock/unreal/ue_math.h>

#include <cameraunlock/unreal/ue_call.h>

// The vector and name parameters of a reflected function. A build lays its
// vectors out in floats or in doubles, so a caller reads the size of one
// component (`real`, 4 or 8) off a parameter of the function it calls and
// hands it to these.
namespace cameraunlock::unreal {

inline std::size_t RealOf(const ScriptFunction& fn, std::string_view param, std::size_t components) {
    const auto size = static_cast<std::size_t>(fn.Param(param).size);
    if (size != components * 4 && size != components * 8)
        throw std::runtime_error(fn.Path() + " " + std::string(param) + " is " + std::to_string(size) + " bytes, not " +
                                 std::to_string(components) + " floats or doubles");
    return size / components;
}

inline void SetVector(ScriptFunction::Frame& f, std::string_view param, std::size_t real,
                      const FVector& v) {
    if (real == 8) f.Set(param, std::array<double, 3>{v.X, v.Y, v.Z});
    else f.Set(param, std::array<float, 3>{static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)});
}

inline FVector GetVector(const ScriptFunction::Frame& f, std::string_view param, std::size_t real) {
    if (real == 8) {
        const auto v = f.Get<std::array<double, 3>>(param);
        return {v[0], v[1], v[2]};
    }
    const auto v = f.Get<std::array<float, 3>>(param);
    return {v[0], v[1], v[2]};
}

inline void SetVector2D(ScriptFunction::Frame& f, std::string_view param, std::size_t real, double x, double y) {
    if (real == 8) f.Set(param, std::array<double, 2>{x, y});
    else f.Set(param, std::array<float, 2>{static_cast<float>(x), static_cast<float>(y)});
}

inline std::array<double, 2> GetVector2D(const ScriptFunction::Frame& f, std::string_view param, std::size_t real) {
    if (real == 8) return f.Get<std::array<double, 2>>(param);
    const auto v = f.Get<std::array<float, 2>>(param);
    return {v[0], v[1]};
}

// `text` as an FName, in the bytes KismetStringLibrary:Conv_StringToName
// (`toName`, called on the library's default object) hands back.
inline std::vector<std::uint8_t> NameOfString(const ScriptFunction& toName, std::uintptr_t library, const std::string& text) {
    const std::wstring wide(text.begin(), text.end());
    const auto length = static_cast<std::int32_t>(wide.size() + 1);
    struct {
        const wchar_t* data;
        std::int32_t num, max;
    } string{wide.c_str(), length, length};
    ScriptFunction::Frame f(toName);
    f.SetBytes("InString", &string, sizeof(string));
    f.Call(library);
    const std::uint8_t* name = f.Bytes("ReturnValue");
    return {name, name + static_cast<std::size_t>(toName.Param("ReturnValue").size)};
}

}  // namespace cameraunlock::unreal
