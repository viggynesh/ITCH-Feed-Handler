// baseline_book: reconstruct the book for one symbol using the std::map
// implementation and print the result. Usage:
//   baseline_book <file.ITCH50> [SYMBOL]
// If SYMBOL is omitted, the busiest symbol in the file is used.

#include <cstdio>
#include <optional>
#include <string>

#include "itch/app_util.hpp"
#include "itch/baseline_book.hpp"
#include "itch/mapped_file.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <file.ITCH50> [SYMBOL]\n", argv[0]);
        return 1;
    }
    std::optional<std::string> want;
    if (argc > 2)
        want = std::string(argv[2]);

    try {
        itch::MappedFile file(argv[1]);
        file.prefault();

        const itch::SymbolScan scan = itch::scan_symbols(file.data(), file.size());
        const itch::Target tgt = itch::select_target(scan, want);

        itch::BaselineBook book({tgt.locate});
        itch::ParseStats stats;
        itch::parse_stream(file.data(), file.size(), book, stats);

        std::printf("baseline order book (std::map price levels)\n");
        itch::print_stats(stats);
        itch::print_book(book, tgt);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
