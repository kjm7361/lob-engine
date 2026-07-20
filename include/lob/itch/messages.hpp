#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace lob::itch {

// All fields are host-endian (decoded from big-endian wire format).
// String fields (stock, mpid, attribution) are exactly the raw wire bytes —
// fixed-width, right-padded with spaces. Use trim_stock() to get a clean view.

struct MsgHeader {
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp_ns;  // 48-bit wire field expanded to 64-bit nanoseconds
};

// S — System Event
struct MsgSystemEvent {
    MsgHeader hdr;
    char      event_code;  // 'O'pen, 'S'tart, 'Q'uote, 'M'arket, 'E'nd, 'C'lose
};

// R — Stock Directory
struct MsgStockDirectory {
    MsgHeader           hdr;
    std::array<char, 8> stock;                // right-padded with spaces
    char                market_category;
    char                financial_status;
    uint32_t            round_lot_size;
    char                round_lots_only;
    char                issue_classification;
    std::array<char, 2> issue_sub_type;
    char                authenticity;
    char                short_sale_threshold;
    char                ipo_flag;
    char                luld_tier;
    char                etp_flag;
    uint32_t            etp_leverage_factor;
    char                inverse_indicator;
};

// A — Add Order (no MPID)
// F — Add Order with MPID  (has_mpid == true, mpid is valid)
struct MsgAddOrder {
    MsgHeader           hdr;
    uint64_t            ref;       // ITCH order reference number
    char                side;      // 'B' = buy, 'S' = sell
    uint32_t            shares;
    std::array<char, 8> stock;     // right-padded with spaces
    uint32_t            price;     // 4 implied decimal places (e.g. 10000 = $1.0000)
    bool                has_mpid;
    std::array<char, 4> mpid;      // valid only when has_mpid == true
};

// E — Order Executed
struct MsgOrderExecuted {
    MsgHeader hdr;
    uint64_t  ref;
    uint32_t  executed_shares;
    uint64_t  match_number;
};

// C — Order Executed with Price
struct MsgOrderExecutedWithPrice {
    MsgHeader hdr;
    uint64_t  ref;
    uint32_t  executed_shares;
    uint64_t  match_number;
    char      printable;    // 'Y' or 'N'
    uint32_t  exec_price;
};

// X — Order Cancel (partial reduction)
struct MsgOrderCancel {
    MsgHeader hdr;
    uint64_t  ref;
    uint32_t  cancelled_shares;
};

// D — Order Delete
struct MsgOrderDelete {
    MsgHeader hdr;
    uint64_t  ref;
};

// U — Order Replace
struct MsgOrderReplace {
    MsgHeader hdr;
    uint64_t  orig_ref;
    uint64_t  new_ref;
    uint32_t  shares;
    uint32_t  price;
};

// P — Non-cross Trade Message
struct MsgTrade {
    MsgHeader           hdr;
    uint64_t            ref;          // order reference (may be 0)
    char                side;         // 'B' or 'S'
    uint32_t            shares;
    std::array<char, 8> stock;
    uint32_t            price;
    uint64_t            match_number;
};

// Trim trailing spaces from a fixed-width stock/mpid/attribution field.
template <size_t N>
[[nodiscard]] std::string_view trim_stock(const std::array<char, N>& arr) noexcept {
    size_t end = N;
    while (end > 0 && arr[end - 1] == ' ') --end;
    return std::string_view{arr.data(), end};
}

}  // namespace lob::itch
