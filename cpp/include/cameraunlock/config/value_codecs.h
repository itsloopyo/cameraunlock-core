#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// The canonical value codecs: how a value is written in a canonical config file and how
// it is read back. Pure, no <windows.h>. CameraUnlock.Core.Config holds the C# twins, and
// data/fixtures/canonical-ini/codecs holds both languages to the same text.
//
// Every codec has the same shape:
//   using Value = ...;
//   CodecParseResult<Value> Parse(std::string_view text) const;
//   std::string Render(const Value&) const;
//   bool Equal(const Value&, const Value&) const;
// Parse takes a value as the canonical reader delivers it, already trimmed of spaces and
// tabs, and never throws. Render writes the canonical text, which Parse reads back as an
// Equal value, and throws std::invalid_argument for a value that would not read back.
// Equal is what verification compares with: floats compare bitwise.
//
// Floats go through std::to_chars and std::from_chars, which must be correctly rounded
// down to the subnormals. Verified with MSVC 19.50 and libstdc++ from GCC 12.5 and 13.5.
// libstdc++ 11 reports every subnormal result as out of range, so value_codecs.cpp refuses
// to compile against it.

namespace cameraunlock::config {

/// What a codec's Parse read. On failure `error` names what was expected, for the
/// diagnostic that also names the line and the value, and `value` is value-initialised.
template <class T>
struct CodecParseResult {
    T value{};
    std::string error;

    bool ok() const { return error.empty(); }
};

namespace detail {

enum class DecimalScan { kOk, kSyntax, kTooLarge };

// Reads -?[0-9]+ into a sign and a magnitude. kTooLarge when the magnitude passes
// unsigned long long.
DecimalScan ScanDecimal(std::string_view text, bool& negative, unsigned long long& magnitude);

bool EqualsAsciiIgnoreCase(std::string_view a, std::string_view b);

// A capital ASCII letter, then ASCII letters and digits.
bool IsPascalCase(std::string_view token);

// "A", "A or B", "A, B or C".
std::string JoinAlternatives(const std::vector<std::string>& items);

}  // namespace detail

/// `true` / `false`. Reads true false 1 0 yes no on off, ASCII case-insensitive.
class BoolCodec {
public:
    using Value = bool;

    CodecParseResult<bool> Parse(std::string_view text) const;
    std::string Render(bool value) const;
    bool Equal(bool a, bool b) const { return a == b; }
};

/// A whole number in decimal, `-` only when negative, no leading zeros. Reads -?[0-9]+
/// (leading zeros and `-0` included) within Int and the codec's inclusive range. No `+`,
/// no hex, no white space.
template <class Int>
class IntCodec {
    static_assert(std::is_integral_v<Int> && !std::is_same_v<Int, bool>,
                  "IntCodec holds an integral type other than bool; BoolCodec holds bool");

public:
    using Value = Int;

    IntCodec() : IntCodec(std::numeric_limits<Int>::min(), std::numeric_limits<Int>::max()) {}

    /// Throws std::invalid_argument when min is above max.
    IntCodec(Int min, Int max) : min_(min), max_(max) {
        if (min > max) {
            throw std::invalid_argument("IntCodec range " + Text(min) + " to " + Text(max) + " has min above max");
        }
    }

    Int min() const { return min_; }
    Int max() const { return max_; }

    CodecParseResult<Int> Parse(std::string_view text) const {
        CodecParseResult<Int> result;
        bool negative = false;
        unsigned long long magnitude = 0;
        if (detail::ScanDecimal(text, negative, magnitude) != detail::DecimalScan::kOk) {
            result.error = Expectation();
            return result;
        }

        Int value = 0;
        if (negative && magnitude != 0) {
            if constexpr (std::is_signed_v<Int>) {
                const unsigned long long limit =
                    static_cast<unsigned long long>(-(static_cast<long long>(std::numeric_limits<Int>::min()) + 1)) + 1;
                if (magnitude > limit) {
                    result.error = Expectation();
                    return result;
                }
                value = static_cast<Int>(-static_cast<long long>(magnitude - 1) - 1);
            } else {
                result.error = Expectation();
                return result;
            }
        } else {
            if (magnitude > static_cast<unsigned long long>(std::numeric_limits<Int>::max())) {
                result.error = Expectation();
                return result;
            }
            value = static_cast<Int>(magnitude);
        }

        if (value < min_ || value > max_) {
            result.error = Expectation();
            return result;
        }
        result.value = value;
        return result;
    }

    /// Throws std::invalid_argument for a value outside the range.
    std::string Render(Int value) const {
        if (value < min_ || value > max_) {
            throw std::invalid_argument(Text(value) + " is outside " + Text(min_) + " to " + Text(max_) +
                                        " and would not read back");
        }
        return Text(value);
    }

    bool Equal(Int a, Int b) const { return a == b; }

private:
    static std::string Text(Int value) {
        if constexpr (std::is_signed_v<Int>) {
            return std::to_string(static_cast<long long>(value));
        } else {
            return std::to_string(static_cast<unsigned long long>(value));
        }
    }

    std::string Expectation() const { return "expected a whole number from " + Text(min_) + " to " + Text(max_); }

    Int min_;
    Int max_;
};

/// `0x` and upper-case hex digits without padding: `0x404`, `0x0`. Reads `0x` or `0X`
/// and 1 to 8 (hex32, std::uint32_t) or 1 to 16 (hex64, std::uint64_t) digits of either
/// case, leading zeros included.
template <class UInt>
class HexCodec {
    static_assert(std::is_same_v<UInt, std::uint32_t> || std::is_same_v<UInt, std::uint64_t>,
                  "HexCodec holds std::uint32_t (hex32) or std::uint64_t (hex64)");

public:
    using Value = UInt;

    CodecParseResult<UInt> Parse(std::string_view text) const;
    std::string Render(UInt value) const;
    bool Equal(UInt a, UInt b) const { return a == b; }
};

extern template class HexCodec<std::uint32_t>;
extern template class HexCodec<std::uint64_t>;
using Hex32Codec = HexCodec<std::uint32_t>;
using Hex64Codec = HexCodec<std::uint64_t>;

/// A float or double, written for people. Of the %.Ng texts (N = 1 to 9 for float, 1 to
/// 17 for double, in the C locale, through std::to_chars) that read back to the same bits,
/// the one with the smallest N that has no exponent, or, when every one has an exponent,
/// the one with the smallest N; then `.0` appended when the text has neither `.` nor `e`.
/// So 1 is `1.0`, 10 is `10.0` rather than `1e+01`, 0.15 is `0.15`, 0.00001 is `1e-05`
/// and -0 is `-0.0`.
///
/// Reads -?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)? through std::from_chars, within the
/// codec's inclusive range. No inf, nan, leading `+`, leading or trailing `.`, comma or hex
/// float. A number too large for the type is invalid, and so is one that is not zero but
/// too close to zero to hold, where std::from_chars reports it out of range rather than
/// rounding it to zero.
template <class F>
class FloatingCodec {
    static_assert(std::is_same_v<F, float> || std::is_same_v<F, double>,
                  "FloatingCodec holds float or double");

public:
    using Value = F;

    /// Every finite value.
    FloatingCodec();

    /// Throws std::invalid_argument when a bound is not finite or min is above max.
    FloatingCodec(F min, F max);

    F min() const { return min_; }
    F max() const { return max_; }

    CodecParseResult<F> Parse(std::string_view text) const;

    /// Throws std::invalid_argument for a value that is not finite or is outside the range.
    std::string Render(F value) const;

    /// Bitwise, so -0.0 and 0.0 differ.
    bool Equal(F a, F b) const;

private:
    std::string RangeExpectation() const;

    F min_;
    F max_;
};

extern template class FloatingCodec<float>;
extern template class FloatingCodec<double>;
using FloatCodec = FloatingCodec<float>;
using DoubleCodec = FloatingCodec<double>;

/// Text, kept as its bytes, so a non-ASCII value survives whatever its encoding. Reads
/// any bytes but a control byte below 0x20 other than tab.
class StringCodec {
public:
    using Value = std::string;

    CodecParseResult<std::string> Parse(std::string_view text) const;

    /// Writes the bytes as they are. Throws std::invalid_argument for a control byte below
    /// 0x20 other than tab, or a leading or trailing space or tab, which the reader trims.
    std::string Render(const std::string& value) const;

    bool Equal(const std::string& a, const std::string& b) const { return a == b; }
};

/// One token of an EnumCodec: the word a file holds, and the enumerator it stands for.
template <class E>
struct EnumToken {
    std::string token;
    E value;
};

/// A word from a closed list, never a number. Written in the declared spelling, read ASCII
/// case-insensitively.
template <class E>
class EnumCodec {
    static_assert(std::is_enum_v<E>, "EnumCodec holds an enum type");

public:
    using Value = E;

    /// Throws std::invalid_argument for an empty list, a token that is not PascalCase (a
    /// capital ASCII letter, then ASCII letters and digits), two tokens equal ASCII
    /// case-insensitively, or two tokens for one enumerator.
    EnumCodec(std::initializer_list<EnumToken<E>> tokens) : tokens_(tokens) {
        if (tokens_.empty()) throw std::invalid_argument("EnumCodec needs at least one token");
        for (std::size_t i = 0; i < tokens_.size(); ++i) {
            if (!detail::IsPascalCase(tokens_[i].token)) {
                throw std::invalid_argument("enum token '" + tokens_[i].token +
                                            "' is not PascalCase: expected a capital letter, then letters and digits");
            }
            for (std::size_t j = 0; j < i; ++j) {
                if (detail::EqualsAsciiIgnoreCase(tokens_[i].token, tokens_[j].token)) {
                    throw std::invalid_argument("enum tokens '" + tokens_[j].token + "' and '" + tokens_[i].token +
                                                "' read the same");
                }
                if (tokens_[i].value == tokens_[j].value) {
                    throw std::invalid_argument("enum tokens '" + tokens_[j].token + "' and '" + tokens_[i].token +
                                                "' name one value");
                }
            }
        }
    }

    const std::vector<EnumToken<E>>& tokens() const { return tokens_; }

    CodecParseResult<E> Parse(std::string_view text) const {
        CodecParseResult<E> result;
        for (const EnumToken<E>& entry : tokens_) {
            if (detail::EqualsAsciiIgnoreCase(text, entry.token)) {
                result.value = entry.value;
                return result;
            }
        }
        std::vector<std::string> words;
        for (const EnumToken<E>& entry : tokens_) words.push_back(entry.token);
        result.error = "expected " + detail::JoinAlternatives(words);
        return result;
    }

    /// Throws std::invalid_argument for a value with no token.
    std::string Render(E value) const {
        for (const EnumToken<E>& entry : tokens_) {
            if (entry.value == value) return entry.token;
        }
        throw std::invalid_argument("enum value " +
                                    std::to_string(static_cast<long long>(static_cast<std::underlying_type_t<E>>(value))) +
                                    " has no token");
    }

    bool Equal(E a, E b) const { return a == b; }

private:
    std::vector<EnumToken<E>> tokens_;
};

/// Red, green, blue and alpha: four floats in [0,1], written `r, g, b, a` with the float
/// rule, read as exactly four comma-separated floats, each trimmed of spaces and tabs.
class ColorCodec {
public:
    using Value = std::array<float, 4>;

    CodecParseResult<Value> Parse(std::string_view text) const;

    /// Throws std::invalid_argument for a component that is not finite or is outside [0,1].
    std::string Render(const Value& value) const;

    /// Bitwise, component by component.
    bool Equal(const Value& a, const Value& b) const;
};

/// A list of hex32, hex64 or string items. Read: an empty value is an empty list;
/// otherwise it is split at `,` and each item, trimmed of spaces and tabs, must be
/// non-empty and read under the item codec. Written: the items joined by `, `.
template <class ItemCodec>
class ListCodec {
    static_assert(std::is_same_v<ItemCodec, Hex32Codec> || std::is_same_v<ItemCodec, Hex64Codec> ||
                      std::is_same_v<ItemCodec, StringCodec>,
                  "ListCodec holds hex32, hex64 or string items");

public:
    using Value = std::vector<typename ItemCodec::Value>;

    CodecParseResult<Value> Parse(std::string_view text) const;

    /// Throws std::invalid_argument for an item the item codec cannot write, or an item
    /// that is empty or holds `,`, which would not read back as one item.
    std::string Render(const Value& value) const;

    bool Equal(const Value& a, const Value& b) const;
};

extern template class ListCodec<Hex32Codec>;
extern template class ListCodec<Hex64Codec>;
extern template class ListCodec<StringCodec>;
using Hex32ListCodec = ListCodec<Hex32Codec>;
using Hex64ListCodec = ListCodec<Hex64Codec>;
using StringListCodec = ListCodec<StringCodec>;

}  // namespace cameraunlock::config
