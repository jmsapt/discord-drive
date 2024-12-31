#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dpp/channel.h>
#include <dpp/cluster.h>
#include <dpp/dispatcher.h>
#include <dpp/dpp.h>
#include <ranges>

#include "src/DiscordDrive.hpp"

using namespace std;

std::vector<std::array<uint8_t, 4096>> input;
std::vector<std::array<uint8_t, 4096>> output;

void fill_data(size_t num_blocks) {
    std::array<uint8_t, 4096> buffer;
    uint32_t x = time(NULL);

    for (auto _ : std::ranges::iota_view{0ul, num_blocks}) {
        for (auto i : std::ranges::iota_view{0, 1024}) {
            ::memcpy(buffer.data() + i * 4, &x, 4);
        }

        input.push_back(buffer);
    }
}

bool validate();

int main(int argc, char *argv[]) {
    char *raw_token = std::getenv("TOKEN");
    if (!raw_token) {
        cerr << "`TOKEN` environment variable not set\n";
        return 1;
    }
    std::string token(raw_token);

    if (argc != 2) {
        cerr << "Usage: <super block num>\n";
        return 1;
    }

    size_t num_blocks = std::stol(argv[1]);

    fill_data(num_blocks);

    cout << "Loading..." << endl;

    // load remove state (should this be bundled into the constructor ... it
    // takes like 20s ... idk)
    constexpr size_t MB     = 1024 * 1024;
    constexpr size_t SBlock = 8 * MB;
    constexpr int Nth       = 17;

    /* Cycle 1 */
    {
        DiscordDrive cache(token, 1);
        cache.load();
        cout << "Write our blocks" << endl;
        for (const auto &[i, b] : std::ranges::enumerate_view{input})
            cache.write(i * Nth * SBlock, std::span(b));

        cout << "Written, flushing..." << endl;
        cache.sync();

        cout << "Flushed, reading..." << endl;
        for (const auto &[i, _] : std::ranges::enumerate_view{output}) {
            cache.read(i * Nth * SBlock, std::span(output[i]));
            int err =
                ::memcmp(input[i].data(), output[i].data(), cache.blocksize());
            if (err) {
                cerr << "Read buffer didn't match written buffer: " << err
                     << "(super block " << i * Nth << ")" << endl;
                return 1;
            }
        }

        cache.sync();
        cache.sync();
        cache.sync();
    }

    /* Cycle 2 */
    {
        DiscordDrive cache(token, 1);
        cache.load();

        cache.write(208896, std::span(input[0]));
        cache.sync();
        cache.write(4096, std::span(input[0]));
        cache.sync();
        cache.write(0, std::span(input[0]));
        cache.sync();
    }

    cout << "Success!" << endl;
    return 0;
}
