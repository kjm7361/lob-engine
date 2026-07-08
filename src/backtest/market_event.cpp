#include "lob/backtest/market_event.hpp"

#include <cmath>
#include <fstream>
#include <random>
#include <sstream>

namespace lob::backtest {

std::vector<MarketEvent> load_csv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return {};

    std::vector<MarketEvent> events;
    std::string line;
    std::getline(file, line);  // skip header row

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tok;

        try {
            uint64_t ts, bid, ask, last, vol;
            std::getline(ss, tok, ','); ts   = std::stoull(tok);
            std::getline(ss, tok, ','); bid  = std::stoull(tok);
            std::getline(ss, tok, ','); ask  = std::stoull(tok);
            std::getline(ss, tok, ','); last = std::stoull(tok);
            std::getline(ss, tok, ','); vol  = std::stoull(tok);

            events.push_back({
                Timestamp{ts},
                Price{static_cast<int64_t>(bid)},
                Price{static_cast<int64_t>(ask)},
                Price{static_cast<int64_t>(last)},
                Quantity{vol},
            });
        } catch (...) {
            continue;  // skip malformed rows
        }
    }

    return events;
}

std::vector<MarketEvent> synthetic_feed(int64_t mid_price, size_t n_events,
                                        uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> noise(0.0, 2.5);

    std::vector<MarketEvent> events;
    events.reserve(n_events);

    double       price  = static_cast<double>(mid_price);
    const double mean   = price;
    const double kappa  = 0.05;  // mean-reversion speed

    for (size_t i = 0; i < n_events; ++i) {
        price += noise(rng) - kappa * (price - mean);
        price  = std::max(price, 2.0);  // floor so bid is always >= 1

        auto mid = static_cast<int64_t>(std::round(price));
        events.push_back({
            Timestamp{i},
            Price{mid - 1},
            Price{mid + 1},
            Price{mid},
            Quantity{100},
        });
    }

    return events;
}

}  // namespace lob::backtest
