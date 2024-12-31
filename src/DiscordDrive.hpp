#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <curl/curl.h>
#include <dpp/cluster.h>
#include <dpp/snowflake.h>
#include <fmt/std.h>
#include <optional>
#include <span>
#include <string>

#include <dpp/dpp.h>

// Non-portable breakpoint macro (intel specific)
#define BREAK() asm("int $3")
#define BREAK_IF(cond)                                                         \
    if (cond) {                                                                \
        BREAK();                                                               \
    }

class DiscordDrive {
    static constexpr int KB = 1024;
    static constexpr int MB = KB * KB;

    static constexpr int NumMessages    = 100; // per channel
    static constexpr int SuperBlockSize = 8 * MB;
    static constexpr int MinorBlockSize = 4 * KB;
    static_assert(SuperBlockSize % MinorBlockSize == 0,
                  "unaligned block sizes");

    const size_t NumChannels;

    using Id = dpp::snowflake;
    struct Message {
        Id id;
        std::string url;
    };
    struct Channel {
        Id id;
        std::array<std::optional<Message>, NumMessages> messages;
    };

    dpp::cluster bot;
    dpp::snowflake server_id;
    std::vector<std::optional<Channel>> channels;

  public:
    struct Slot {
        enum CacheState { Empty, Clean, Dirty } state{CacheState::Empty};
        std::vector<std::uint8_t> block{};
        size_t off;
        std::optional<Id> msg_id;
        std::optional<Id> chl_id;
    } slot;

    DiscordDrive(const std::string &token, size_t num_channels);
    ~DiscordDrive();
    void load();
    void read(size_t offset, std::span<std::uint8_t, MinorBlockSize> buffer);
    void write(size_t offset,
               std::span<const std::uint8_t, MinorBlockSize> data);
    void sync();
    size_t blocksize() const;
    size_t size() const;

  private:
    // load a block from remote into cache
    void fetch(size_t off);
};
