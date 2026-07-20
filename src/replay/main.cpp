#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "lob/replay/replay.hpp"

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " <file.itch[.gz]>"
                 " [--symbols AAPL,MSFT,...]"
                 " [--limit N]\n";
}

static std::unordered_set<std::string> split_symbols(const std::string& s) {
    std::unordered_set<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ','))
        if (!tok.empty()) out.insert(tok);
    return out;
}

int main(int argc, char* argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    std::string                      path;
    lob::replay::Config              cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--symbols") {
            if (++i >= argc) { usage(argv[0]); return 1; }
            cfg.symbols = split_symbols(argv[i]);
        } else if (arg == "--limit") {
            if (++i >= argc) { usage(argv[0]); return 1; }
            cfg.message_limit = static_cast<uint64_t>(std::stoull(argv[i]));
        } else if (path.empty()) {
            path = arg;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            usage(argv[0]);
            return 1;
        }
    }

    if (path.empty()) { usage(argv[0]); return 1; }

    lob::replay::ReplayEngine engine(std::move(cfg));

    lob::replay::ReplayStats stats;
    try {
        stats = engine.run(path);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    std::cout << "=== ITCH Replay Summary ===\n"
              << "Messages processed : " << stats.messages_processed << '\n'
              << "Orders added       : " << stats.orders_added       << '\n'
              << "Orders executed    : " << stats.orders_executed     << '\n'
              << "Orders cancelled   : " << stats.orders_cancelled    << '\n'
              << "Orders deleted     : " << stats.orders_deleted      << '\n'
              << "Orders replaced    : " << stats.orders_replaced     << '\n'
              << "Unknown refs       : " << stats.unknown_refs        << '\n'
              << '\n'
              << "=== Book Snapshots ===\n";

    auto snaps = engine.snapshots();
    if (snaps.empty()) {
        std::cout << "(no symbols tracked)\n";
    } else {
        for (const auto& s : snaps) {
            std::cout << s.symbol << ": ";
            if (s.best_bid)
                std::cout << "bid=" << lob::to_int(*s.best_bid)
                          << " (" << s.bid_depth << " shs)  ";
            else
                std::cout << "bid=- ";
            if (s.best_ask)
                std::cout << "ask=" << lob::to_int(*s.best_ask)
                          << " (" << s.ask_depth << " shs)";
            else
                std::cout << "ask=-";
            std::cout << '\n';
        }
    }

    return 0;
}
