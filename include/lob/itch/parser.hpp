#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "messages.hpp"

namespace lob::itch {

// Callbacks for each decoded message type.
// Leave any callback null to skip that message type entirely.
struct ItchHandler {
    std::function<void(const MsgSystemEvent&)>            on_system_event;
    std::function<void(const MsgStockDirectory&)>         on_stock_directory;
    std::function<void(const MsgAddOrder&)>               on_add_order;
    std::function<void(const MsgOrderExecuted&)>          on_order_executed;
    std::function<void(const MsgOrderExecutedWithPrice&)> on_order_executed_with_price;
    std::function<void(const MsgOrderCancel&)>            on_order_cancel;
    std::function<void(const MsgOrderDelete&)>            on_order_delete;
    std::function<void(const MsgOrderReplace&)>           on_order_replace;
    std::function<void(const MsgTrade&)>                  on_trade;
};

struct ParseStats {
    uint64_t messages_parsed;   // recognized messages dispatched to a callback
    uint64_t messages_skipped;  // unknown message type bytes
    uint64_t bytes_read;
};

// Parse an ITCH 5.0 binary file.
// Detects gzip by checking for the 0x1f 0x8b magic bytes (not the extension).
// Streams in 64 KB chunks; never loads the whole file into memory.
// Throws std::runtime_error if the file cannot be opened.
ParseStats parse_file(const std::string& path, const ItchHandler& handler);

}  // namespace lob::itch
