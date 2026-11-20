// File Name:     file.h
// Author:        Arthur Ufongene
// Course:        CS60: Computer Networks
// Assignment:    Lab 4: Physical Layer
// Date:          November 2026

// Description: interface for splitting files into chunk messages for sending and
// reassembling received chunks into files

#ifndef DIAL_UP_FILE_H
#define DIAL_UP_FILE_H

#include "message.h"
#include "util.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// File transfer: splitting a file into chunk payloads (send side), finding the
// file for /file, and reassembling and saving chunks (receive side).
// Chunk layout is in message.h.
namespace file {

inline constexpr size_t kChunkHeader  = 6;
inline constexpr size_t kMaxNameLen   = 64;
inline constexpr size_t kMaxChunks    = 65535;
inline constexpr size_t kChunkDataCap = message::kMaxPayload - kChunkHeader;   // 249, chunks 1..N-1

// A transfer with no new chunk for this long is discarded.
inline constexpr std::chrono::duration<double> kTransferTimeout{3 * message::kFrameSeconds};

// Where received files are saved (created by main on startup).
inline const std::filesystem::path kReceivedDir = "received";

/**
 * @brief Split a file into chunk payloads (each <= message::kMaxPayload bytes).
 *        The name goes in chunk 0 only.
 * @throws std::length_error if the file needs more than kMaxChunks chunks.
 */
std::vector<std::vector<uint8_t>> buildChunks(const std::string& name,
                                              const std::vector<uint8_t>& data,
                                              uint8_t transferId);

/** @brief Basename of the file to send, cut to kMaxNameLen bytes (keeping the extension). */
std::string wireName(const std::filesystem::path& path);

/**
 * @brief An existing path, else a recursive search of the current directory by
 *        filename. Prints "not found" or the list of matches if not exactly one.
 */
std::optional<std::filesystem::path> find(const std::string& arg);

// Collects file chunks by (sender, transfer id) and saves each file to
// kReceivedDir once every chunk has arrived. Chunks may arrive in any order;
// duplicates are ignored.
class assembler {
public:
    /** @brief Add one file-chunk payload (type byte included) from `from`. */
    void addChunk(const MAC_Address& from, const std::vector<uint8_t>& payload);

    /** @brief Drop transfers that have gone quiet for kTransferTimeout. */
    void expire();

private:
    using Clock = std::chrono::steady_clock;
    using Key = std::pair<MAC_Address, uint8_t>;   // (sender, transfer id)

    struct Transfer {
        std::optional<std::string> name;
        std::vector<std::optional<std::vector<uint8_t>>> chunks;
        size_t have = 0;
        Clock::time_point last;
    };

    std::map<Key, Transfer> transfers_;

    static void save(const MAC_Address& from, const Transfer& t);
};

}  // namespace file

#endif // DIAL_UP_FILE_H
