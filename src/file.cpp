#include "file.h"

#include "console.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;
using console::macToString;
using console::out;

namespace file {
namespace {

// Strip any directories from a received name so it can't escape kReceivedDir.
fs::path safeName(const std::string& received) {
    fs::path name = fs::path(received).filename();
    if (name.empty() || name == "." || name == "..") name = "file";
    return name;
}

// received/name.ext, or received/name (1).ext, (2), ... if it already exists.
fs::path uniquePath(const fs::path& name) {
    fs::path p = kReceivedDir / name;
    for (int i = 1; fs::exists(p); ++i)
        p = kReceivedDir / (name.stem().string() + " (" + std::to_string(i) + ")" + name.extension().string());
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------
// Send side
// ---------------------------------------------------------------------------

std::vector<std::vector<uint8_t>> buildChunks(const std::string& name,
                                              const std::vector<uint8_t>& data,
                                              uint8_t transferId) {
    const size_t firstCap = kChunkDataCap - 1 - name.size();
    const size_t rest = data.size() > firstCap ? data.size() - firstCap : 0;
    const size_t count = 1 + (rest + kChunkDataCap - 1) / kChunkDataCap;
    if (count > kMaxChunks) throw std::length_error("file too large (max 65535 chunks)");

    std::vector<std::vector<uint8_t>> chunks;
    size_t pos = 0;
    for (size_t i = 0; i < count; ++i) {
        std::vector<uint8_t> p{message::kTypeFile, transferId,
                               static_cast<uint8_t>(i >> 8), static_cast<uint8_t>(i),
                               static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count)};
        size_t cap = kChunkDataCap;
        if (i == 0) {
            p.push_back(static_cast<uint8_t>(name.size()));
            p.insert(p.end(), name.begin(), name.end());
            cap = firstCap;
        }
        const size_t n = std::min(cap, data.size() - pos);
        p.insert(p.end(), data.begin() + static_cast<long>(pos), data.begin() + static_cast<long>(pos + n));
        pos += n;
        chunks.push_back(std::move(p));
    }
    return chunks;
}

std::string wireName(const fs::path& path) {
    std::string name = path.filename().string();
    if (name.size() <= kMaxNameLen) return name;
    const std::string ext = path.extension().string();
    if (ext.size() >= kMaxNameLen) return name.substr(0, kMaxNameLen);
    return path.stem().string().substr(0, kMaxNameLen - ext.size()) + ext;
}

std::optional<fs::path> find(const std::string& arg) {
    std::error_code ec;
    if (fs::is_regular_file(arg, ec)) return fs::path(arg);

    std::vector<fs::path> matches;
    const fs::path wanted = fs::path(arg).filename();
    fs::recursive_directory_iterator it(".", fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->path().filename() == wanted && it->is_regular_file(ec)) matches.push_back(it->path());
    }

    if (matches.empty()) {
        out("not found: " + arg);
        return std::nullopt;
    }
    if (matches.size() > 1) {
        out("several files named " + arg + "; give a path:");
        for (const auto& m : matches) out("  " + m.string());
        return std::nullopt;
    }
    return matches.front();
}

// ---------------------------------------------------------------------------
// Receive side
// ---------------------------------------------------------------------------

void assembler::addChunk(const MAC_Address& from, const std::vector<uint8_t>& p) {
    if (p.size() < kChunkHeader) return;
    const uint8_t id = p[1];
    const size_t index = static_cast<size_t>(p[2] << 8 | p[3]);
    const size_t count = static_cast<size_t>(p[4] << 8 | p[5]);
    if (count == 0 || index >= count) return;

    size_t dataStart = kChunkHeader;
    std::optional<std::string> name;
    if (index == 0) {
        if (p.size() < kChunkHeader + 1) return;
        const size_t nameLen = p[kChunkHeader];
        dataStart = kChunkHeader + 1 + nameLen;
        if (p.size() < dataStart) return;
        name.emplace(p.begin() + kChunkHeader + 1, p.begin() + static_cast<long>(dataStart));
    }

    const Key key{from, id};
    auto it = transfers_.find(key);
    if (it != transfers_.end() && it->second.chunks.size() != count) {
        transfers_.erase(it);   // id reused by a new transfer
        it = transfers_.end();
    }
    if (it == transfers_.end()) {
        Transfer t;
        t.chunks.resize(count);
        it = transfers_.emplace(key, std::move(t)).first;
    }

    Transfer& t = it->second;
    t.last = Clock::now();
    if (t.chunks[index]) return;   // duplicate
    t.chunks[index].emplace(p.begin() + static_cast<long>(dataStart), p.end());
    ++t.have;
    if (name) t.name = *name;

    if (t.have < count) {
        out("receiving " + t.name.value_or("?") + " from " + macToString(from) + ": " +
            std::to_string(t.have) + "/" + std::to_string(count));
        return;
    }
    save(from, t);
    transfers_.erase(it);
}

void assembler::expire() {
    const auto now = Clock::now();
    for (auto it = transfers_.begin(); it != transfers_.end();) {
        const Transfer& t = it->second;
        if (now - t.last > kTransferTimeout) {
            out("file " + t.name.value_or("?") + " from " + macToString(it->first.first) +
                " incomplete: got " + std::to_string(t.have) + "/" + std::to_string(t.chunks.size()) +
                " chunks, discarded");
            it = transfers_.erase(it);
        } else {
            ++it;
        }
    }
}

void assembler::save(const MAC_Address& from, const Transfer& t) {
    const fs::path path = uniquePath(safeName(t.name.value_or("file")));
    std::ofstream f(path, std::ios::binary);
    for (const auto& c : t.chunks) f.write(reinterpret_cast<const char*>(c->data()), static_cast<long>(c->size()));
    if (!f) {
        console::err("could not write " + path.string());
        return;
    }
    out("received " + path.filename().string() + " from " + macToString(from));
}

}  // namespace file
