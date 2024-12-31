#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <curl/curl.h>
#include <dpp/cluster.h>
#include <dpp/snowflake.h>
#include <fmt/std.h>
#include <iostream>
#include <optional>
#include <semaphore>
#include <span>
#include <stdexcept>
#include <string>

#include <dpp/dpp.h>
#include <string_view>

#include "src/DiscordDrive.hpp"

using std::optional;

size_t curlCb(void *contents, size_t size, size_t nmemb, void *ptr) {
    std::uint8_t **my_ptr{static_cast<std::uint8_t **>(ptr)};
    size_t total_size = size * nmemb;
    ::memcpy(*my_ptr, contents, total_size);

    // advance ptr in stack frame to hold our state
    *my_ptr += total_size;
    return total_size;
};

DiscordDrive::DiscordDrive(const std::string &token, size_t num_channels)
    : NumChannels(num_channels), bot{token} {
    slot.block.resize(SuperBlockSize);
};
DiscordDrive::~DiscordDrive() { 
    std::cout << "Destroying DiscordDrive object, final sync..." << std::endl;
    sync();
    std::cout << "Done!" << std::endl;
}

size_t DiscordDrive::blocksize() const { return MinorBlockSize; }
size_t DiscordDrive::size() const {
    return NumChannels * NumMessages * SuperBlockSize;
}

void DiscordDrive::load() {
    using namespace std;
    std::binary_semaphore sem{false};

    // get channels
    auto second = [&](const dpp::confirmation_callback_t &event) {
        if (event.is_error())
            throw std::runtime_error("Failed to query all channels");

        for (const auto _ : std::ranges::iota_view(0ul, NumChannels))
            channels.push_back(std::nullopt);

        for (auto x : event.get<dpp::channel_map>()) {
            size_t index = std::stol(x.second.name);
            if (index >= channels.size())
                continue;

            channels[index] = optional<Channel>{x.second.id};
        }

        sem.release();
    };

    auto first = [&](const dpp::ready_t &event) {
        server_id = event.guilds.front();
        cout << "Server id : " << server_id.str() << '\n';
        bot.channels_get(server_id, second);
    };
    bot.on_ready(first);
    bot.start();

    // block on channels
    std::counting_semaphore<> count{0};

    // get message ids
    for (const auto &[i, c] : std::ranges::enumerate_view(channels)) {
        if (!c.has_value()) {
            count.release();
            continue;
            // TODO make this stricter
            // throw std::runtime_error(std::format("Missing block at {}",
            // i));
        }

        auto third = [&, i](const dpp::confirmation_callback_t &event) {
            if (event.is_error())
                throw std::runtime_error(
                    std::format("Failed to get messages from channel {}", i));
            auto messages = event.get<dpp::message_map>();

            for (const auto &m : messages) {
                if (m.second.attachments.empty()) {
                    // TODO make this just delete the message instead
                    throw std::runtime_error(
                        std::format("Corrupt message in channel {}", i));
                }

                std::string url = m.second.attachments.front().url;
                Id id           = m.second.id;
                size_t index    = std::stol(m.second.content);

                c->messages[index] = optional(Message{id, url});
            }
            count.release();
        };
        bot.messages_get(c->id, 0, 0, 0, NumMessages, third);
    }

    // get record of all msg ids
    for (const auto _ : std::ranges::iota_view(0ul, NumChannels))
        count.acquire();

    // done, state loaded from remote
};

// read a block
void DiscordDrive::read(size_t offset,
                        std::span<std::uint8_t, MinorBlockSize> buffer) {
    if (offset % MinorBlockSize != 0)
        throw std::runtime_error(
            std::format("Cache read at offset {} not aligned to {}", offset,
                        MinorBlockSize));

    // load block into the cache
    fetch((offset / SuperBlockSize) * SuperBlockSize);

    ::memcpy(buffer.data(), slot.block.data() + (offset % SuperBlockSize),
             MinorBlockSize);
}

void DiscordDrive::write(size_t offset,
                         std::span<const std::uint8_t, MinorBlockSize> data) {
    if (data.size() != MinorBlockSize || offset % MinorBlockSize != 0)
        throw std::runtime_error(
            std::format("Cache cache_write at offset {} either not aligned "
                        "to or of size {}",
                        offset, MinorBlockSize));

    // load block into the cache
    fetch((offset / SuperBlockSize) * SuperBlockSize);
    ::memcpy(slot.block.data() + offset % SuperBlockSize, data.data(),
             data.size());
    slot.state = Slot::Dirty;
};

void DiscordDrive::sync() {
    using namespace std;
    cout << "line " << __LINE__ << endl;
    // no work to do (empty or clean slots)
    if (slot.state != Slot::Dirty)
        return;

    cout << "line " << __LINE__ << endl;
    std::counting_semaphore<> wait{0};

    // write new msg
    assert(slot.off % SuperBlockSize == 0);
    auto chl_num = slot.off / (SuperBlockSize * NumMessages);
    auto msg_num = slot.off % (SuperBlockSize * NumMessages) / SuperBlockSize;
    assert(chl_num < NumChannels && msg_num < NumMessages);

    cout << "line " << __LINE__ << endl;
    // delete old msg
    cout << "line " << __LINE__ << endl;
    if (channels[chl_num]->messages[msg_num].has_value()) {
        auto cb = [&](const dpp::confirmation_callback_t &event) {
            if (event.is_error())
                throw std::runtime_error(
                    "Failed to delete message whilst flushing the cache");

            slot.msg_id = std::nullopt;
            channels[chl_num]->messages[msg_num] = std::nullopt;

            wait.release();
        };

        // pretend like this can't fail, if it does we may have to purge the
        // server
        assert(slot.chl_id != std::nullopt);
        bot.message_delete(channels[chl_num]->messages[msg_num]->id, *(slot.chl_id), cb);
        wait.acquire();
    }

    cout << "line " << __LINE__ << endl;
    slot.chl_id = optional(channels[chl_num]->id);
    auto msg = dpp::message(*(slot.chl_id), std::to_string(msg_num));
    std::string_view content(reinterpret_cast<char *>(slot.block.data()),
                             slot.block.size());
    cout << "line " << __LINE__ << endl;
    msg.add_file("block", content);

    cout << "line " << __LINE__ << endl;
    auto cb = [&](const dpp::confirmation_callback_t &event) {
        if (event.is_error())
            throw std::runtime_error(
                "Failed to create message whilst flushing the cache");

        auto message = event.get<dpp::message>();
        if (message.attachments.front().size == 0)
            throw std::runtime_error("Wrote zero size block");

        auto url    = message.attachments.front().url;
        auto msg_id = message.id;

        channels[chl_num]->messages[msg_num] = optional(Message{msg_id, url});
        slot.state = Slot::Clean;

        wait.release();
    };
    cout << "line " << __LINE__ << endl;

    bot.message_create(msg, cb);
    wait.acquire();
};

void DiscordDrive::fetch(size_t off) {
    // cache hit
    assert(off % MinorBlockSize == 0);
    if (off >= slot.off && off < slot.off + SuperBlockSize &&
        slot.state != Slot::Empty)
        return;

    // miss and dirty
    if (slot.state == Slot::Dirty)
        sync();
    assert(slot.state != Slot::Dirty);

    assert(off % SuperBlockSize == 0);
    auto chl_num = off / (SuperBlockSize * NumMessages);
    auto msg_num = off % (SuperBlockSize * NumMessages) / SuperBlockSize;

    assert(chl_num < NumChannels && msg_num < NumMessages);
    auto &channel = *(channels[chl_num]);

    // fresh block
    if (!channel.messages[msg_num].has_value()) {
        slot.chl_id = optional(channel.id);
        slot.msg_id = std::nullopt;
        slot.off    = off;

        ::memset(slot.block.data(), 0, SuperBlockSize);
        slot.state = Slot::Clean;
        return;
    }

    auto &message = *(channel.messages[msg_num]);

    CURL *curl;
    CURLcode res;
    curl = curl_easy_init();

    if (!curl)
        throw std::runtime_error("Couldn't instaniate curl");

    std::binary_semaphore wait{false};

    // try max of n times
    constexpr int NTries = 2;
    for (int i = 0; i < NTries; ++i) {
        // stack locals for stateful, partial curl requests
        std::uint8_t *ptr = slot.block.data();

        curl_easy_setopt(curl, CURLOPT_URL, message.url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ptr);
        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            curl_easy_cleanup(curl);

            slot.chl_id = optional(channel.id);
            slot.msg_id = optional(message.id);
            slot.off    = off;
            slot.state  = Slot::Clean;

            return;
        }
    }

    curl_easy_cleanup(curl);
    throw std::runtime_error(std::format(
        "Failed to fetch block from remote after trying {} times", NTries));
}
