// main.cpp — Dial-Up: send and receive text and files over sound.
//
// Usage:  dial_up [-s] [-r] [-v] [--mac aa:bb:cc:dd:ee:ff]
//   -s     send only        -r  receive only      (neither, or both: send and receive)
//   -v     verbose demodulator diagnostics on stderr
//   --mac  use this MAC instead of the hardware one (e.g. two peers on one machine)
//
// REPL commands: /send <text>, /file <name or path>, /help, /quit
//
// Sending is src/sender, receiving (on a background thread) is src/receiver,
// file chunking / reassembly is src/file.

#include "src/console.h"
#include "src/data_link_layer.h"
#include "src/file.h"
#include "src/receiver.h"
#include "src/sender.h"
#include "src/speaker.h"
#include "src/util.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

using console::out;

namespace {

// ---------------------------------------------------------------------------
// Command line
// ---------------------------------------------------------------------------

struct Options {
    bool send = false;
    bool recv = false;
    bool verbose = false;
    std::optional<MAC_Address> mac;
};

std::optional<MAC_Address> parseMac(const std::string& s) {
    MAC_Address mac{};
    unsigned v[6];
    char tail;
    if (std::sscanf(s.c_str(), "%2x:%2x:%2x:%2x:%2x:%2x%c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &tail) != 6)
        return std::nullopt;
    for (int i = 0; i < 6; ++i) mac[i] = static_cast<uint8_t>(v[i]);
    return mac;
}

void usage(const char* argv0) {
    std::fprintf(stderr, "usage: %s [-s] [-r] [-v] [--mac aa:bb:cc:dd:ee:ff]\n"
                         "  -s  send only   -r  receive only   (neither or both: send and receive)\n"
                         "  -v  verbose demodulator output\n", argv0);
}

std::optional<Options> parseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--mac" && i + 1 < argc) {
            o.mac = parseMac(argv[++i]);
            if (!o.mac) { std::fprintf(stderr, "bad MAC address: %s\n", argv[i]); return std::nullopt; }
        } else if (a.size() > 1 && a[0] == '-' && a[1] != '-') {
            for (size_t k = 1; k < a.size(); ++k) {   // allows combined flags like -sr
                if (a[k] == 's') o.send = true;
                else if (a[k] == 'r') o.recv = true;
                else if (a[k] == 'v') o.verbose = true;
                else { usage(argv[0]); return std::nullopt; }
            }
        } else {
            usage(argv[0]);
            return std::nullopt;
        }
    }
    if (!o.send && !o.recv) o.send = o.recv = true;
    return o;
}

// ---------------------------------------------------------------------------
// REPL
// ---------------------------------------------------------------------------

void printHelp(bool canSend) {
    out("commands:");
    if (canSend) {
        out("  /send [@mac] <text>   send a text message (up to 254 bytes)");
        out("  /file [@mac] <name>   send a file (a path, or a filename to search for under the current directory)");
        out("                        @mac (e.g. @aa:bb:cc:dd:ee:ff) sends to one device; without it, to everyone");
    }
    out("  /help                 show this list");
    out("  /quit                 exit");
}

void repl(bool canSend, sender* tx) {
    printHelp(canSend);
    std::string line;
    while (true) {
        console::prompt();
        if (!std::getline(std::cin, line)) break;   // Ctrl-D

        const size_t sp = line.find(' ');
        const std::string cmd = line.substr(0, sp);
        std::string arg = sp == std::string::npos ? "" : line.substr(sp + 1);

        if (cmd.empty()) continue;
        if (cmd == "/quit") break;
        if (cmd == "/help") { printHelp(canSend); continue; }
        if (cmd == "/send" || cmd == "/file") {
            if (!tx) { out("sending is disabled (-r)"); continue; }
            // Optional leading @mac picks the destination; default is broadcast.
            MAC_Address dest = BROADCAST_MAC;
            if (!arg.empty() && arg[0] == '@') {
                const size_t end = arg.find(' ');
                const std::string macText = arg.substr(1, end == std::string::npos ? std::string::npos : end - 1);
                const std::optional<MAC_Address> m = parseMac(macText);
                if (!m) { out("bad MAC address: " + macText + " (expected @aa:bb:cc:dd:ee:ff)"); continue; }
                dest = *m;
                arg = end == std::string::npos ? "" : arg.substr(end + 1);
            }
            if (cmd == "/send") {
                tx->sendText(dest, arg);
            } else if (arg.empty()) {
                out("usage: /file [@mac] <name or path>");
            } else if (const auto path = file::find(arg)) {
                tx->sendFile(dest, *path);
            }
            continue;
        }
        out("unknown command: " + cmd + " (try /help)");
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::optional<Options> opts = parseArgs(argc, argv);
    if (!opts) return 1;

    std::error_code ec;
    std::filesystem::create_directories(file::kReceivedDir, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create %s: %s\n", file::kReceivedDir.string().c_str(), ec.message().c_str());
        return 1;
    }

    const std::optional<MAC_Address> mac = opts->mac ? opts->mac : get_MAC();
    if (!mac) {
        std::fprintf(stderr, "no MAC address found for this device; pass one with --mac\n");
        return 1;
    }
    const data_link_layer link(*mac);
    speaker spk;

    out(std::string("dial-up ") + console::macToString(*mac) + "  mode: " +
        (opts->send && opts->recv ? "send + receive" : opts->send ? "send only" : "receive only") +
        (opts->recv ? "  (files saved to " + file::kReceivedDir.string() + "/)" : ""));

    std::atomic<bool> stop{false};
    std::optional<receiver> rx;
    std::thread rxThread;
    if (opts->recv) {
        rx.emplace(link, spk, *mac, opts->verbose);
        rxThread = std::thread([&] { rx->run(stop); });
    }

    std::optional<sender> tx;
    if (opts->send) tx.emplace(link, spk);
    repl(opts->send, tx ? &*tx : nullptr);

    stop = true;
    if (rxThread.joinable()) rxThread.join();
    return 0;
}
