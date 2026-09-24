#include "cameraunlock/config/value_codecs.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <system_error>
#include <utility>

#if defined(_GLIBCXX_RELEASE) && _GLIBCXX_RELEASE < 12
#error "the float codecs need libstdc++ from GCC 12 or later: in GCC 11 std::from_chars reports every subnormal result as out of range, so a subnormal float would not read back"
#endif

namespace cameraunlock::config {

namespace detail {

DecimalScan ScanDecimal(std::string_view text, bool& negative, unsigned long long& magnitude) {
    negative = !text.empty() && text[0] == '-';
    const std::size_t first = negative ? 1 : 0;
    if (first == text.size()) return DecimalScan::kSyntax;

    magnitude = 0;
    bool too_large = false;
    for (std::size_t i = first; i < text.size(); ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') return DecimalScan::kSyntax;
        const unsigned digit = static_cast<unsigned>(c - '0');
        if (magnitude > (std::numeric_limits<unsigned long long>::max() - digit) / 10) {
            too_large = true;
        } else {
            magnitude = magnitude * 10 + digit;
        }
    }
    return too_large ? DecimalScan::kTooLarge : DecimalScan::kOk;
}

bool EqualsAsciiIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char x = a[i];
        char y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

bool IsPascalCase(std::string_view token) {
    if (token.empty() || token[0] < 'A' || token[0] > 'Z') return false;
    for (const char c : token) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

std::string JoinAlternatives(const std::vector<std::string>& items) {
    std::string text;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) text += (i + 1 == items.size()) ? " or " : ", ";
        text += items[i];
    }
    return text;
}

}  // namespace detail

namespace {

bool IsSpaceOrTab(char c) { return c == ' ' || c == '\t'; }

std::string_view Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && IsSpaceOrTab(text[begin])) ++begin;
    while (end > begin && IsSpaceOrTab(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

std::vector<std::string_view> SplitAtCommas(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t comma = text.find(',', start);
        if (comma == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, comma - start));
        start = comma + 1;
    }
}

std::string Quote(std::string_view text) { return "'" + std::string(text) + "'"; }

std::string HexByte(unsigned char byte) {
    static const char kDigits[] = "0123456789ABCDEF";
    std::string text = "0x";
    text += kDigits[byte >> 4];
    text += kDigits[byte & 0x0F];
    return text;
}

bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// -?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?
bool MatchesNumberGrammar(std::string_view text) {
    std::size_t i = 0;
    const std::size_t n = text.size();
    if (i < n && text[i] == '-') ++i;
    const std::size_t integer_start = i;
    while (i < n && IsDigit(text[i])) ++i;
    if (i == integer_start) return false;
    if (i < n && text[i] == '.') {
        ++i;
        const std::size_t fraction_start = i;
        while (i < n && IsDigit(text[i])) ++i;
        if (i == fraction_start) return false;
    }
    if (i < n && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < n && (text[i] == '+' || text[i] == '-')) ++i;
        const std::size_t exponent_start = i;
        while (i < n && IsDigit(text[i])) ++i;
        if (i == exponent_start) return false;
    }
    return i == n;
}

// For a text of the number grammar whose value is not zero: whether its magnitude is at
// least 1, which tells an overflow from an underflow when std::from_chars reports either.
bool MagnitudeAtLeastOne(std::string_view text) {
    std::size_t i = (text[0] == '-') ? 1 : 0;
    long long first_digit_power = 0;
    bool found = false;
    for (; i < text.size() && IsDigit(text[i]); ++i) {
        if (!found && text[i] != '0') {
            found = true;
            first_digit_power = -1;
        }
        if (found) ++first_digit_power;
    }
    if (i < text.size() && text[i] == '.') {
        long long position = 0;
        for (++i; i < text.size() && IsDigit(text[i]); ++i) {
            --position;
            if (!found && text[i] != '0') {
                found = true;
                first_digit_power = position;
            }
        }
    }
    long long exponent = 0;
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        const bool exponent_negative = text[i] == '-';
        if (text[i] == '+' || text[i] == '-') ++i;
        for (; i < text.size(); ++i) {
            if (exponent < 1000000000LL) exponent = exponent * 10 + (text[i] - '0');
        }
        if (exponent_negative) exponent = -exponent;
    }
    return first_digit_power + exponent >= 0;
}

template <class F>
struct FloatTraits;

template <>
struct FloatTraits<float> {
    using Bits = std::uint32_t;
    static constexpr int kMaxPrecision = 9;
    static constexpr const char* kDenormMinText = "1e-45";
};

template <>
struct FloatTraits<double> {
    using Bits = std::uint64_t;
    static constexpr int kMaxPrecision = 17;
    static constexpr const char* kDenormMinText = "5e-324";
};

template <class F>
typename FloatTraits<F>::Bits BitsOf(F value) {
    typename FloatTraits<F>::Bits bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

template <class F>
std::string PrecisionText(F value, int precision) {
    char buffer[64];
    const std::to_chars_result written =
        std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::general, precision);
    if (written.ec != std::errc()) throw std::logic_error("std::to_chars could not write a finite value in 64 bytes");
    return std::string(buffer, written.ptr);
}

template <class F>
bool ReadsBackAs(const std::string& text, F value) {
    F read{};
    const std::from_chars_result result =
        std::from_chars(text.data(), text.data() + text.size(), read, std::chars_format::general);
    return result.ec == std::errc() && result.ptr == text.data() + text.size() && BitsOf(read) == BitsOf(value);
}

template <class F>
std::string RenderFinite(F value) {
    std::string chosen;
    for (int precision = 1; precision <= FloatTraits<F>::kMaxPrecision; ++precision) {
        std::string text = PrecisionText(value, precision);
        if (!ReadsBackAs(text, value)) continue;
        const bool has_exponent = text.find('e') != std::string::npos;
        if (!has_exponent) {
            chosen = std::move(text);
            break;
        }
        if (chosen.empty()) chosen = std::move(text);
    }
    if (chosen.empty()) {
        throw std::logic_error("no %.Ng text of a finite value read back to the same bits; std::from_chars or "
                               "std::to_chars is not correctly rounded");
    }
    if (chosen.find('.') == std::string::npos && chosen.find('e') == std::string::npos) chosen += ".0";
    return chosen;
}

template <class F>
const char* TypeName();
template <>
const char* TypeName<float>() { return "float"; }
template <>
const char* TypeName<double>() { return "double"; }

}  // namespace

CodecParseResult<bool> BoolCodec::Parse(std::string_view text) const {
    CodecParseResult<bool> result;
    for (const char* word : {"true", "1", "yes", "on"}) {
        if (detail::EqualsAsciiIgnoreCase(text, word)) {
            result.value = true;
            return result;
        }
    }
    for (const char* word : {"false", "0", "no", "off"}) {
        if (detail::EqualsAsciiIgnoreCase(text, word)) return result;
    }
    result.error = "expected true or false";
    return result;
}

std::string BoolCodec::Render(bool value) const { return value ? "true" : "false"; }

template <class UInt>
CodecParseResult<UInt> HexCodec<UInt>::Parse(std::string_view text) const {
    constexpr std::size_t kMaxDigits = sizeof(UInt) * 2;
    CodecParseResult<UInt> result;
    const std::size_t digits = text.size() < 2 ? 0 : text.size() - 2;
    if (text.size() < 3 || text[0] != '0' || (text[1] != 'x' && text[1] != 'X') || digits > kMaxDigits) {
        result.error = "expected 0x and 1 to " + std::to_string(kMaxDigits) + " hex digits, such as 0x404";
        return result;
    }
    UInt value = 0;
    for (std::size_t i = 2; i < text.size(); ++i) {
        const char c = text[i];
        unsigned digit;
        if (c >= '0' && c <= '9') {
            digit = static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = static_cast<unsigned>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = static_cast<unsigned>(c - 'A' + 10);
        } else {
            result.error = "expected 0x and 1 to " + std::to_string(kMaxDigits) + " hex digits, such as 0x404";
            return result;
        }
        value = static_cast<UInt>((value << 4) | digit);
    }
    result.value = value;
    return result;
}

template <class UInt>
std::string HexCodec<UInt>::Render(UInt value) const {
    char buffer[2 + sizeof(UInt) * 2];
    const std::to_chars_result written = std::to_chars(buffer, buffer + sizeof buffer, value, 16);
    std::string text = "0x";
    for (const char* p = buffer; p != written.ptr; ++p) {
        text += (*p >= 'a' && *p <= 'f') ? static_cast<char>(*p - 'a' + 'A') : *p;
    }
    return text;
}

template class HexCodec<std::uint32_t>;
template class HexCodec<std::uint64_t>;

template <class F>
FloatingCodec<F>::FloatingCodec() : min_(std::numeric_limits<F>::lowest()), max_(std::numeric_limits<F>::max()) {}

template <class F>
FloatingCodec<F>::FloatingCodec(F min, F max) : min_(min), max_(max) {
    if (!std::isfinite(min) || !std::isfinite(max) || min > max) {
        throw std::invalid_argument(std::string(TypeName<F>()) +
                                    " codec range needs finite bounds with min not above max");
    }
}

template <class F>
std::string FloatingCodec<F>::RangeExpectation() const {
    return "expected a number from " + RenderFinite(min_) + " to " + RenderFinite(max_);
}

template <class F>
CodecParseResult<F> FloatingCodec<F>::Parse(std::string_view text) const {
    CodecParseResult<F> result;
    const bool full_range = BitsOf(min_) == BitsOf(std::numeric_limits<F>::lowest()) &&
                            BitsOf(max_) == BitsOf(std::numeric_limits<F>::max());
    if (!MatchesNumberGrammar(text)) {
        result.error = full_range ? "expected a number such as 1.0, 0.15 or 1e-05" : RangeExpectation();
        return result;
    }

    F value{};
    const std::from_chars_result read =
        std::from_chars(text.data(), text.data() + text.size(), value, std::chars_format::general);
    if (read.ec == std::errc::result_out_of_range) {
        if (MagnitudeAtLeastOne(text) || min_ > F(0) || max_ < F(0)) {
            result.error = full_range ? "expected a number from " + RenderFinite(std::numeric_limits<F>::lowest()) +
                                            " to " + RenderFinite(std::numeric_limits<F>::max())
                                      : RangeExpectation();
        } else {
            result.error =
                std::string("expected 0.0 or a number no closer to zero than ") + FloatTraits<F>::kDenormMinText;
        }
        return result;
    }
    if (read.ec != std::errc() || read.ptr != text.data() + text.size()) {
        throw std::logic_error("std::from_chars refused '" + std::string(text) + "', which the number grammar allows");
    }
    if (value < min_ || value > max_) {
        result.error = RangeExpectation();
        return result;
    }
    result.value = value;
    return result;
}

template <class F>
std::string FloatingCodec<F>::Render(F value) const {
    if (!std::isfinite(value) || value < min_ || value > max_) {
        throw std::invalid_argument(std::string(TypeName<F>()) + " value is not finite or is outside " +
                                    RenderFinite(min_) + " to " + RenderFinite(max_) + ", and would not read back");
    }
    return RenderFinite(value);
}

template <class F>
bool FloatingCodec<F>::Equal(F a, F b) const {
    return BitsOf(a) == BitsOf(b);
}

template class FloatingCodec<float>;
template class FloatingCodec<double>;

CodecParseResult<std::string> StringCodec::Parse(std::string_view text) const {
    CodecParseResult<std::string> result;
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 && byte != '\t') {
            result.error = "holds the control byte " + HexByte(byte) + ": expected text with no control character but tab";
            return result;
        }
    }
    result.value = std::string(text);
    return result;
}

std::string StringCodec::Render(const std::string& value) const {
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 && byte != '\t') {
            throw std::invalid_argument("text holding the control byte " + HexByte(byte) + " would not read back");
        }
    }
    if (!value.empty() && (IsSpaceOrTab(value.front()) || IsSpaceOrTab(value.back()))) {
        throw std::invalid_argument("text starting or ending in a space or tab would not read back, because the reader trims it");
    }
    return value;
}

CodecParseResult<ColorCodec::Value> ColorCodec::Parse(std::string_view text) const {
    CodecParseResult<Value> result;
    const std::vector<std::string_view> parts = SplitAtCommas(text);
    if (parts.size() != 4) {
        result.error = "expected four numbers from 0.0 to 1.0 separated by commas, such as 1.0, 0.5, 0.0, 1.0";
        return result;
    }
    const FloatCodec component(0.0f, 1.0f);
    for (std::size_t i = 0; i < 4; ++i) {
        const std::string_view item = Trim(parts[i]);
        const CodecParseResult<float> read = component.Parse(item);
        if (!read.ok()) {
            result.value = Value{};
            result.error = "item " + std::to_string(i + 1) + " " + Quote(item) + ": " + read.error;
            return result;
        }
        result.value[i] = read.value;
    }
    return result;
}

std::string ColorCodec::Render(const Value& value) const {
    const FloatCodec component(0.0f, 1.0f);
    std::string text;
    for (std::size_t i = 0; i < 4; ++i) {
        if (i > 0) text += ", ";
        text += component.Render(value[i]);
    }
    return text;
}

bool ColorCodec::Equal(const Value& a, const Value& b) const {
    for (std::size_t i = 0; i < 4; ++i) {
        if (BitsOf(a[i]) != BitsOf(b[i])) return false;
    }
    return true;
}

template <class ItemCodec>
CodecParseResult<typename ListCodec<ItemCodec>::Value> ListCodec<ItemCodec>::Parse(std::string_view text) const {
    CodecParseResult<Value> result;
    if (Trim(text).empty()) return result;

    const ItemCodec codec{};
    const std::vector<std::string_view> parts = SplitAtCommas(text);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::string_view item = Trim(parts[i]);
        if (item.empty()) {
            result.value.clear();
            result.error = "item " + std::to_string(i + 1) + " is empty: expected a value between commas";
            return result;
        }
        CodecParseResult<typename ItemCodec::Value> read = codec.Parse(item);
        if (!read.ok()) {
            result.value.clear();
            result.error = "item " + std::to_string(i + 1) + " " + Quote(item) + ": " + read.error;
            return result;
        }
        result.value.push_back(std::move(read.value));
    }
    return result;
}

template <class ItemCodec>
std::string ListCodec<ItemCodec>::Render(const Value& value) const {
    const ItemCodec codec{};
    std::string text;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const std::string item = codec.Render(value[i]);
        if (item.empty() || item.find(',') != std::string::npos) {
            throw std::invalid_argument("list item " + std::to_string(i + 1) +
                                        " is empty or holds ',' and would not read back as one item");
        }
        if (i > 0) text += ", ";
        text += item;
    }
    return text;
}

template <class ItemCodec>
bool ListCodec<ItemCodec>::Equal(const Value& a, const Value& b) const {
    if (a.size() != b.size()) return false;
    const ItemCodec codec{};
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!codec.Equal(a[i], b[i])) return false;
    }
    return true;
}

template class ListCodec<Hex32Codec>;
template class ListCodec<Hex64Codec>;
template class ListCodec<StringCodec>;

}  // namespace cameraunlock::config
