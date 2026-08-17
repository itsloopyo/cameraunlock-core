#pragma once
namespace kiero {
    enum class Status { Success = 0, UnknownError = 1, NotSupportedError = 2 };
    enum class RenderType { None, D3D9, D3D10, D3D11, D3D12, OpenGL, Vulkan };
    inline Status init(RenderType) { return Status::Success; }
    inline void shutdown() {}
    // Real kiero takes void*; MSVC allows fn-ptr -> void* as an extension, gcc
    // does not, so the stub takes anything.
    template <typename F> inline Status bind(int, void**, F) { return Status::Success; }
    inline void unbind(int) {}
}
