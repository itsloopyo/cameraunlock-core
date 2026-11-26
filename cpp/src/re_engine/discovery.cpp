// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/re_engine/discovery.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

#include "../memory/hde_names.h"
#include "../../../vendor/minhook/src/hde/hde64.h"

namespace cameraunlock::re_engine::discovery {

std::string Hex(std::uintptr_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

namespace {

std::string Signed(std::ptrdiff_t v) { return (v < 0 ? "-" : "+") + Hex(static_cast<std::uintptr_t>(v < 0 ? -v : v)); }

}  // namespace

Vm PickVm(const std::vector<Load>& loads) {
    std::map<std::uintptr_t, std::map<std::uintptr_t, int>> byGlobal;
    for (const Load& l : loads) ++byGlobal[l.global][l.call];
    std::vector<std::pair<int, std::uintptr_t>> counts;
    for (const auto& [global, calls] : byGlobal) {
        int n = 0;
        for (const auto& [call, c] : calls) n += c;
        counts.emplace_back(n, global);
    }
    std::sort(counts.rbegin(), counts.rend());
    if (counts.empty() || counts[0].first <= 10)
        throw std::runtime_error("the script VM: no global is loaded more than ten times before a call taking -1 (" +
                                 std::to_string(loads.size()) + " loads seen)");
    if (counts.size() > 1 && counts[1].first * 2 >= counts[0].first)
        throw std::runtime_error("the script VM: globals " + Hex(counts[0].second) + " and " + Hex(counts[1].second) + " are loaded " +
                                 std::to_string(counts[0].first) + " and " + std::to_string(counts[1].first) +
                                 " times before a call taking -1, too close to tell which is the VM");
    const std::uintptr_t global = counts[0].second;
    const int total = counts[0].first;
    std::uintptr_t call = 0;
    int votes = 0;
    for (const auto& [target, c] : byGlobal[global])
        if (c > votes) votes = c, call = target;
    if (votes * 10 < total * 9)
        throw std::runtime_error("the script VM at " + Hex(global) + ": its " + std::to_string(total) + " loads call " +
                                 std::to_string(byGlobal[global].size()) + " functions, the most called by " + std::to_string(votes));
    return {global, call};
}

std::vector<std::uintptr_t> PointerReads(const std::uint8_t* code, std::size_t size, std::uintptr_t at) {
    std::vector<std::uintptr_t> reads;
    for (std::size_t i = 0; i < size;) {
        hde64s hs;
        const unsigned len = hde64_disasm(code + i, &hs);
        if (hs.flags & F_ERROR) break;
        const bool ripRelative = (hs.flags & F_MODRM) && hs.modrm_mod == 0 && hs.modrm_rm == 5;
        const bool load = hs.opcode == 0x8B || (hs.opcode == 0x83 && hs.modrm_reg == 7) || (hs.opcode == 0x81 && hs.modrm_reg == 7);
        if (ripRelative && hs.rex_w && load) {
            // The /GS cookie is loaded and xored with rsp straight away; it is no object's global.
            hde64s next;
            hde64_disasm(code + i + len, &next);
            const bool cookie = !(next.flags & F_ERROR) && next.rex_w && (next.opcode == 0x31 || next.opcode == 0x33) && next.modrm_mod == 3 &&
                                (next.modrm_reg == 4 || next.modrm_rm == 4);
            if (!cookie) reads.push_back(at + i + len + static_cast<std::int32_t>(hs.disp.disp32));
        }
        if (hs.opcode == 0xC3 || hs.opcode == 0xC2 || hs.opcode == 0xE9 || hs.opcode == 0xEB) break;
        i += len;
    }
    return reads;
}

InstanceGlobal PickInstanceGlobal(const std::vector<std::vector<std::uintptr_t>>& reads) {
    std::map<std::uintptr_t, int> readers;
    int reading = 0;
    for (const std::vector<std::uintptr_t>& method : reads) {
        if (method.empty()) continue;
        ++reading;
        for (const std::uintptr_t global : std::set<std::uintptr_t>(method.begin(), method.end())) ++readers[global];
    }
    std::uintptr_t best = 0;
    int most = 0;
    for (const auto& [global, n] : readers)
        if (n > most) most = n, best = global;
    if (most < 3 || most * 2 <= reading)
        throw std::runtime_error("the instance: " + std::to_string(reads.size()) + " static methods, " + std::to_string(reading) +
                                 " of them read a pointer from a global, and the most read global" +
                                 (most ? " (" + Hex(best) + ")" : "") + " is read by " + std::to_string(most));
    return {best, most, reading};
}

std::optional<EntryList> FindEntryList(std::size_t size, const ObjectReader& read) {
    constexpr const char* kStages[] = {"WaitRendering", "BeginRendering", "EndRendering"};
    std::set<std::size_t> described;
    std::map<std::string, std::vector<std::size_t>> stages;
    for (std::size_t at = 0; at + sizeof(void*) <= size; at += sizeof(void*)) {
        const std::string text = read.text(at);
        if (text.empty()) continue;
        described.insert(at);
        for (const char* stage : kStages)
            if (text == stage) stages[stage].push_back(at);
    }
    for (const char* stage : kStages) {
        const auto it = stages.find(stage);
        if (it == stages.end()) return std::nullopt;
        if (it->second.size() > 1) {
            std::string at;
            for (const std::size_t o : it->second) at += " +" + Hex(o);
            throw std::runtime_error(std::string("via.Application names ") + stage + " " + std::to_string(it->second.size()) +
                                     " times, at" + at);
        }
    }
    const std::size_t wait = stages[kStages[0]][0], begin = stages[kStages[1]][0], end = stages[kStages[2]][0];
    const std::size_t low = std::min({wait, begin, end}), high = std::max({wait, begin, end});
    const auto apart = [](std::size_t a, std::size_t b) { return a > b ? a - b : b - a; };

    EntryList list;
    for (std::size_t stride = sizeof(void*); stride <= high - low; stride += sizeof(void*)) {
        if (apart(wait, begin) % stride || apart(end, begin) % stride) continue;
        std::size_t first = begin, last = begin;
        while (first >= stride && described.count(first - stride)) first -= stride;
        while (described.count(last + stride)) last += stride;
        if (first > low || last < high) continue;
        list = {first, stride, (last - first) / stride + 1, 0};
        break;
    }
    if (!list.stride)
        throw std::runtime_error("via.Application: no stride puts WaitRendering (+" + Hex(wait) + "), BeginRendering (+" + Hex(begin) +
                                 ") and EndRendering (+" + Hex(end) + ") on one run of descriptions");

    std::vector<std::ptrdiff_t> funcs;
    const auto stride = static_cast<std::ptrdiff_t>(list.stride);
    for (std::ptrdiff_t at = -stride + static_cast<std::ptrdiff_t>(sizeof(void*)); at < stride; at += sizeof(void*)) {
        if (at == 0) continue;
        bool everyEntry = true;
        for (std::size_t i = 0; i < list.count && everyEntry; ++i) {
            const std::ptrdiff_t slot = static_cast<std::ptrdiff_t>(list.first + i * list.stride) + at;
            everyEntry = slot >= 0 && static_cast<std::size_t>(slot) + sizeof(void*) <= size && read.code(static_cast<std::size_t>(slot));
        }
        if (everyEntry) funcs.push_back(at);
    }
    if (funcs.size() != 1) {
        std::string at;
        for (const std::ptrdiff_t f : funcs) at += " " + Signed(f);
        throw std::runtime_error("via.Application's " + std::to_string(list.count) + " stages, " + Hex(list.stride) +
                                 " apart: " + std::to_string(funcs.size()) +
                                 " places beside the description point at code in every entry" + (funcs.empty() ? "" : ":" + at));
    }
    list.func = funcs[0];
    return list;
}

std::optional<std::size_t> MatrixPlace::See(const std::uint8_t* object, std::size_t size, const float (&matrix)[16]) {
    std::vector<std::size_t> left;
    for (std::size_t at = 0; at + sizeof(matrix) <= size; at += sizeof(float))
        if ((!places_ || std::find(places_->begin(), places_->end(), at) != places_->end()) &&
            std::memcmp(object + at, matrix, sizeof(matrix)) == 0)
            left.push_back(at);
    ++frames_;
    if (left.empty()) {
        std::string was;
        if (places_)
            for (const std::size_t o : *places_) was += " +" + Hex(o);
        throw std::runtime_error("no place in the " + Hex(size) + " bytes holds the matrix the getter gave on frame " +
                                 std::to_string(frames_) + (was.empty() ? "" : "; it was at" + was + " until then"));
    }
    places_ = std::move(left);
    if (places_->size() == 1) return places_->front();
    return std::nullopt;
}

}  // namespace cameraunlock::re_engine::discovery
