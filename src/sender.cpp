#include "sender.h"

#include "console.h"
#include "convolutional.h"
#include "file.h"
#include "fsk.h"
#include "interleaver.h"
#include "message.h"

#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>

using console::out;

sender::sender(const data_link_layer& link, const speaker& spk)
    : link_(link), spk_(spk), nextId_(static_cast<uint8_t>(std::random_device{}())) {}

namespace {
// " to aa:bb:..." for a direct message, "" for broadcast.
std::string toWhom(const MAC_Address& dest) {
    return dest == BROADCAST_MAC ? "" : " to " + console::macToString(dest);
}
}  // namespace

bool sender::send(const MAC_Address& dest, const std::vector<uint8_t>& payload) {
    const std::vector<uint8_t> frame = link_.encode(dest, payload);
    const std::vector<uint8_t> coded = convolutional::encode(interleaver::encode(rs_.encode(frame)));
    return spk_.encode(fsk::encode(coded, message::kCodedBits));
}

void sender::sendText(const MAC_Address& dest, const std::string& text) {
    if (text.empty()) { out("usage: /send [@mac] <text>"); return; }
    if (text.size() > message::kMaxText) {
        out("message too long: " + std::to_string(text.size()) + " bytes (max " +
            std::to_string(message::kMaxText) + ")");
        return;
    }
    std::vector<uint8_t> p{message::kTypeText};
    p.insert(p.end(), text.begin(), text.end());

    out("sending text" + toWhom(dest) + " (~" + std::to_string(static_cast<int>(message::kFrameSeconds + 0.5)) + " s)...");
    out(send(dest, p) ? "sent" : "send failed (audio error)");
}

void sender::sendFile(const MAC_Address& dest, const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { out("cannot open " + path.string()); return; }
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    const std::string name = file::wireName(path);
    std::vector<std::vector<uint8_t>> chunks;
    try {
        chunks = file::buildChunks(name, data, nextId_++);
    } catch (const std::length_error& e) {
        out(e.what());
        return;
    }

    out("sending " + name + toWhom(dest) + " (" + std::to_string(data.size()) + " bytes) in " +
        std::to_string(chunks.size()) + " frame" + (chunks.size() == 1 ? "" : "s") +
        " (~" + std::to_string(static_cast<int>(static_cast<double>(chunks.size()) * message::kFrameSeconds + 0.5)) +
        " s)...");
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (!send(dest, chunks[i])) { out("send failed (audio error)"); return; }
        if (chunks.size() > 1) out("  sent " + std::to_string(i + 1) + "/" + std::to_string(chunks.size()));
    }
    out("sent " + name);
}
