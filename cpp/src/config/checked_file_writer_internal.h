#pragma once

#include "cameraunlock/os/win32.h"

// Not installed and not API: WriteFileChecked's fault-injection seam, for tests.

#include <cameraunlock/config/checked_file_writer.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#if CAMERAUNLOCK_WIN32

namespace cameraunlock::detail {

/// Run before each step with the step and the path it acts on. A nonzero return fails that
/// step with that Win32 error, without running it. Returning 0 after changing the files
/// races the step instead.
using CheckedWriteFault = std::function<std::uint32_t(CheckedWriteStep, const std::wstring&)>;

CheckedWriteResult WriteFileCheckedWithFault(const std::wstring& path,
                                             const std::optional<std::string>& expected,
                                             const std::string& candidate,
                                             const CheckedWriteFault& fault);

}  // namespace cameraunlock::detail

#endif  // CAMERAUNLOCK_WIN32
