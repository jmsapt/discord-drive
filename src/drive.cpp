#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "src/DiscordDrive.hpp"
#include <atomic>
#include <nbdkit-plugin.h>
#include <ranges>

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

static std::atomic<int> count = 0;

size_t NumChannels;
std::string Token;

DiscordDrive *get_dd(void *ptr) { return static_cast<DiscordDrive *>(ptr); }

int dd_config(const char *key, const char *value) {
    char *raw_token = std::getenv("TOKEN");
    if (!raw_token) {
        nbdkit_error("unknown parameter: %s", key);
        return -1;
    }
    Token = std::string{raw_token};

    if (strcmp(key, "channels") == 0) {
        NumChannels = std::stol(value);
    }
    else {
        nbdkit_error("unknown parameter: %s", key);
        return -1;
    }

    if (NumChannels == 0) {
        nbdkit_error("Num channels cannot be zero");
        return -1;
    }

    return 0;
}

int dd_config_complete() {
    if (NumChannels == 0 || Token.length() == 0)
        return -1;

    return 0;
}

void *dd_open(int readonly) {
    assert(count == 0);
    auto dd = new DiscordDrive(Token, NumChannels);
    dd->load();
    return dd;
}

void dd_close(void *handle) {
    auto dd = get_dd(handle);
    dd->sync();
    --count;
    delete dd;
}

int64_t dd_get_size(void *handle) { return get_dd(handle)->size(); }
int dd_blocksize(void *handle, uint32_t *minimum, uint32_t *preferred,
                 uint32_t *maximum) {
    auto dd = get_dd(handle);

    *minimum   = dd->blocksize();
    *preferred = dd->blocksize();
    *maximum   = dd->blocksize();

    return 0;
}

int dd_flush(void *handle) {
    get_dd(handle)->sync();
    return 0;
}

int dd_pread(void *handle, void *buf, uint32_t count, uint64_t offset) {
    auto dd = get_dd(handle);

    if (offset % dd->blocksize() || count % dd->blocksize())
        return -1;

    for (auto i : std::ranges::iota_view{0ul, count / dd->blocksize()}) {
        size_t partial = i * dd->blocksize();
        // TODO remove the hard coding
        dd->read(offset + partial,
                 std::span<uint8_t, 4096>(static_cast<uint8_t *>(buf) + partial,
                                          4096));
    }

    return 0;
}

int dd_pwrite(void *handle, const void *buf, uint32_t count, uint64_t offset) {
    auto dd = get_dd(handle);

    if (offset % dd->blocksize() || count % dd->blocksize())
        return -1;

    for (auto i : std::ranges::iota_view{0ul, count / dd->blocksize()}) {
        size_t partial = i * dd->blocksize();
        dd->write(offset + partial,
                  std::span<const uint8_t, 4096>(
                      static_cast<const uint8_t *>(buf) + partial, 4096));
    }

    return 0;
}

extern "C" {

int disable(void *) { return 0; }
int enable(void *) { return 1; }

static struct nbdkit_plugin plugin = {
    .name            = "discord-drive",
    .config          = dd_config,
    .config_complete = dd_config_complete,

    .open      = dd_open,
    .close     = dd_close,
    .get_size  = dd_get_size,
    .can_write = enable,
    .pread     = dd_pread,
    .pwrite    = dd_pwrite,
    .flush     = dd_flush,

    .can_zero       = disable,
    .can_fua        = disable,
    .can_multi_conn = disable,
    .can_extents    = disable,
    .can_cache      = enable,
    .can_fast_zero  = disable,

};

NBDKIT_REGISTER_PLUGIN(plugin)
}
