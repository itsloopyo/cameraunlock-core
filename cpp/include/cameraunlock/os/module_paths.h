#pragma once

#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

// Where the mod is on disk. Everything a mod reads or writes - its INI, its
// log, its vendored data - hangs off one of these two directories, and getting
// either wrong sends the config to a path the user will never find.
//
// Three sharp edges are handled here so no caller has to remember them:
//
//   - GetModuleFileName TRUNCATES instead of failing, and reports the buffer
//     size rather than the required one, so a MAX_PATH buffer turns a game
//     installed under a long path into a dormant mod on a machine where
//     nothing is wrong. The buffer grows until the name fits.
//   - substr(0, npos) on a path with no separator yields the whole path, and a
//     "directory" of "" then turns the INI path into "\HeadTracking.ini",
//     which is the root of the current drive. DirectoryOf refuses instead.
//   - ANSI narrowing best-fit maps by default: a character the code page
//     cannot encode is quietly replaced by one that looks similar, so a
//     directory can narrow to the name of a DIFFERENT directory that exists,
//     and the config is then read from and written to the wrong one.
//     NarrowToAnsi refuses instead.
namespace cameraunlock::os {

#ifdef _WIN32

/// Directory part of `module_path`, WITHOUT a trailing separator. Both '\\' and
/// '/' count as separators. Returns false, leaving `directory` untouched, when
/// the path carries no separator at all - see the header note.
bool DirectoryOf(const std::wstring& module_path, std::wstring& directory);

/// UTF-16 -> the process ANSI code page. Returns false, leaving `narrow`
/// untouched, when any character has no representation. On a Unicode code page
/// (UTF-8, UTF-7, GB18030) no character can be lost, and WideCharToMultiByte
/// rejects the no-best-fit flags outright for those, so they are converted
/// without them.
bool NarrowToAnsi(const std::wstring& wide, std::string& narrow);

/// Full path of a loaded module, or of the host EXE when `module` is null.
/// Empty on failure. Grows its buffer up to the NT path limit, so a deep
/// install path resolves rather than truncating.
std::wstring ModuleFilePath(HMODULE module);

/// Directory containing the DLL this call is compiled into, WITHOUT a trailing
/// separator. Empty on failure.
///
/// The default resolves the module from the address of a function in this
/// translation unit. cameraunlock is a static library, so that address is
/// inside the consumer's own DLL - which is the whole point: a mod asking
/// where it lives must not be told where the EXE lives. Pass an explicit
/// HMODULE to ask about a different module.
std::wstring SelfModuleDirectory(HMODULE module = nullptr);

/// Directory containing the running EXE, WITHOUT a trailing separator. Empty
/// on failure.
std::wstring HostExeDirectory();

/// ANSI forms of the two above, for the ANSI-only layers a mod still has
/// (IniReader wraps GetPrivateProfile*A). Empty when the directory cannot be
/// resolved OR when it has no ANSI form - a caller must skip its INI in that
/// case, not fall back to a relative path, because the alternative is a
/// best-fit name pointing at somebody else's folder.
std::string SelfModuleDirectoryNarrow(HMODULE module = nullptr);
std::string HostExeDirectoryNarrow();

#endif  // _WIN32

}  // namespace cameraunlock::os
