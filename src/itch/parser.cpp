#include "lob/itch/parser.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <zlib.h>

namespace lob::itch {

// ── Big-endian field decoders ─────────────────────────────────────────────────

static inline uint16_t read_u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

static inline uint32_t read_u32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

// ITCH timestamps are 48-bit big-endian nanoseconds; expand to 64-bit.
static inline uint64_t read_u48(const uint8_t* p) noexcept {
    return (static_cast<uint64_t>(p[0]) << 40) |
           (static_cast<uint64_t>(p[1]) << 32) |
           (static_cast<uint64_t>(p[2]) << 24) |
           (static_cast<uint64_t>(p[3]) << 16) |
           (static_cast<uint64_t>(p[4]) << 8)  |
            static_cast<uint64_t>(p[5]);
}

static inline uint64_t read_u64(const uint8_t* p) noexcept {
    return (static_cast<uint64_t>(p[0]) << 56) |
           (static_cast<uint64_t>(p[1]) << 48) |
           (static_cast<uint64_t>(p[2]) << 40) |
           (static_cast<uint64_t>(p[3]) << 32) |
           (static_cast<uint64_t>(p[4]) << 24) |
           (static_cast<uint64_t>(p[5]) << 16) |
           (static_cast<uint64_t>(p[6]) << 8)  |
            static_cast<uint64_t>(p[7]);
}

// Header occupies bytes 1-10 of every message body (byte 0 is the type tag).
static inline MsgHeader read_header(const uint8_t* p) noexcept {
    return MsgHeader{
        .stock_locate    = read_u16(p + 1),
        .tracking_number = read_u16(p + 3),
        .timestamp_ns    = read_u48(p + 5),
    };
}

// ── Per-type decoders + dispatch ──────────────────────────────────────────────

static void dispatch(const uint8_t* msg, uint16_t len,
                     const ItchHandler& h, ParseStats& stats) {
    if (len == 0) return;
    const char type = static_cast<char>(msg[0]);

    switch (type) {
        case 'S': {
            if (len < 12) { ++stats.messages_skipped; return; }
            if (h.on_system_event) {
                h.on_system_event(MsgSystemEvent{
                    read_header(msg),
                    static_cast<char>(msg[11]),
                });
            }
            break;
        }
        case 'R': {
            if (len < 39) { ++stats.messages_skipped; return; }
            if (h.on_stock_directory) {
                MsgStockDirectory m{};
                m.hdr = read_header(msg);
                std::memcpy(m.stock.data(), msg + 11, 8);
                m.market_category      = static_cast<char>(msg[19]);
                m.financial_status     = static_cast<char>(msg[20]);
                m.round_lot_size       = read_u32(msg + 21);
                m.round_lots_only      = static_cast<char>(msg[25]);
                m.issue_classification = static_cast<char>(msg[26]);
                m.issue_sub_type[0]    = static_cast<char>(msg[27]);
                m.issue_sub_type[1]    = static_cast<char>(msg[28]);
                m.authenticity         = static_cast<char>(msg[29]);
                m.short_sale_threshold = static_cast<char>(msg[30]);
                m.ipo_flag             = static_cast<char>(msg[31]);
                m.luld_tier            = static_cast<char>(msg[32]);
                m.etp_flag             = static_cast<char>(msg[33]);
                m.etp_leverage_factor  = read_u32(msg + 34);
                m.inverse_indicator    = static_cast<char>(msg[38]);
                h.on_stock_directory(m);
            }
            break;
        }
        case 'A': {
            if (len < 36) { ++stats.messages_skipped; return; }
            if (h.on_add_order) {
                MsgAddOrder m{};
                m.hdr      = read_header(msg);
                m.ref      = read_u64(msg + 11);
                m.side     = static_cast<char>(msg[19]);
                m.shares   = read_u32(msg + 20);
                std::memcpy(m.stock.data(), msg + 24, 8);
                m.price    = read_u32(msg + 32);
                m.has_mpid = false;
                h.on_add_order(m);
            }
            break;
        }
        case 'F': {
            if (len < 40) { ++stats.messages_skipped; return; }
            if (h.on_add_order) {
                MsgAddOrder m{};
                m.hdr      = read_header(msg);
                m.ref      = read_u64(msg + 11);
                m.side     = static_cast<char>(msg[19]);
                m.shares   = read_u32(msg + 20);
                std::memcpy(m.stock.data(), msg + 24, 8);
                m.price    = read_u32(msg + 32);
                m.has_mpid = true;
                std::memcpy(m.mpid.data(), msg + 36, 4);
                h.on_add_order(m);
            }
            break;
        }
        case 'E': {
            if (len < 31) { ++stats.messages_skipped; return; }
            if (h.on_order_executed) {
                h.on_order_executed(MsgOrderExecuted{
                    read_header(msg),
                    read_u64(msg + 11),
                    read_u32(msg + 19),
                    read_u64(msg + 23),
                });
            }
            break;
        }
        case 'C': {
            if (len < 36) { ++stats.messages_skipped; return; }
            if (h.on_order_executed_with_price) {
                h.on_order_executed_with_price(MsgOrderExecutedWithPrice{
                    read_header(msg),
                    read_u64(msg + 11),
                    read_u32(msg + 19),
                    read_u64(msg + 23),
                    static_cast<char>(msg[31]),
                    read_u32(msg + 32),
                });
            }
            break;
        }
        case 'X': {
            if (len < 23) { ++stats.messages_skipped; return; }
            if (h.on_order_cancel) {
                h.on_order_cancel(MsgOrderCancel{
                    read_header(msg),
                    read_u64(msg + 11),
                    read_u32(msg + 19),
                });
            }
            break;
        }
        case 'D': {
            if (len < 19) { ++stats.messages_skipped; return; }
            if (h.on_order_delete) {
                h.on_order_delete(MsgOrderDelete{
                    read_header(msg),
                    read_u64(msg + 11),
                });
            }
            break;
        }
        case 'U': {
            if (len < 35) { ++stats.messages_skipped; return; }
            if (h.on_order_replace) {
                h.on_order_replace(MsgOrderReplace{
                    read_header(msg),
                    read_u64(msg + 11),
                    read_u64(msg + 19),
                    read_u32(msg + 27),
                    read_u32(msg + 31),
                });
            }
            break;
        }
        case 'P': {
            if (len < 44) { ++stats.messages_skipped; return; }
            if (h.on_trade) {
                MsgTrade m{};
                m.hdr          = read_header(msg);
                m.ref          = read_u64(msg + 11);
                m.side         = static_cast<char>(msg[19]);
                m.shares       = read_u32(msg + 20);
                std::memcpy(m.stock.data(), msg + 24, 8);
                m.price        = read_u32(msg + 32);
                m.match_number = read_u64(msg + 36);
                h.on_trade(m);
            }
            break;
        }
        default:
            ++stats.messages_skipped;
            return;
    }
    ++stats.messages_parsed;
}

// ── Unified stream reader (plain file or gzip) ────────────────────────────────

class StreamReader {
   public:
    explicit StreamReader(const std::string& path) {
        // Detect gzip from the magic bytes, not the file extension.
        {
            std::ifstream probe(path, std::ios::binary);
            if (!probe) throw std::runtime_error("Cannot open: " + path);
            uint8_t magic[2] = {0, 0};
            probe.read(reinterpret_cast<char*>(magic), 2);
            is_gz_ = (magic[0] == 0x1f && magic[1] == 0x8b);
        }
        if (is_gz_) {
            gz_ = gzopen(path.c_str(), "rb");
            if (gz_ == nullptr) throw std::runtime_error("gzopen failed: " + path);
        } else {
            plain_.open(path, std::ios::binary);
            if (!plain_) throw std::runtime_error("Cannot open: " + path);
        }
    }

    StreamReader(const StreamReader&) = delete;
    StreamReader& operator=(const StreamReader&) = delete;

    ~StreamReader() {
        if (gz_ != nullptr) gzclose(gz_);
    }

    // Returns bytes written to buf, or 0 at EOF / error.
    size_t read(uint8_t* buf, size_t n) {
        if (is_gz_) {
            int r = gzread(gz_, buf, static_cast<unsigned int>(n));
            return (r < 0) ? 0u : static_cast<size_t>(r);
        }
        plain_.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(n));
        return static_cast<size_t>(plain_.gcount());
    }

   private:
    bool          is_gz_{false};
    gzFile        gz_{nullptr};
    std::ifstream plain_;
};

// ── Streaming parse loop ──────────────────────────────────────────────────────

static constexpr size_t kChunkSize  = 65536;
static constexpr size_t kBufInitial = kChunkSize * 2;  // ample for any message

ParseStats parse_file(const std::string& path, const ItchHandler& handler) {
    StreamReader reader(path);
    ParseStats   stats{};

    std::vector<uint8_t> buf(kBufInitial);
    size_t filled = 0;  // bytes available in buf[0..filled)
    size_t pos    = 0;  // current parse cursor

    // Ensure at least `need` bytes are available starting at pos.
    // Compacts the buffer and refills from the stream as necessary.
    // Returns false only when the stream is exhausted before `need` bytes arrive.
    auto ensure = [&](size_t need) -> bool {
        while (filled - pos < need) {
            if (pos > 0) {
                std::memmove(buf.data(), buf.data() + pos, filled - pos);
                filled -= pos;
                pos = 0;
            }
            if (buf.size() < filled + kChunkSize) {
                buf.resize(filled + kChunkSize);
            }
            size_t got = reader.read(buf.data() + filled, kChunkSize);
            stats.bytes_read += got;
            if (got == 0) return false;
            filled += got;
        }
        return true;
    };

    while (true) {
        if (!ensure(2)) break;

        // 2-byte big-endian length prefix = body length (excluding the prefix itself)
        uint16_t msg_len = read_u16(buf.data() + pos);
        pos += 2;

        if (msg_len == 0) continue;

        if (!ensure(msg_len)) break;  // truncated file — stop gracefully

        dispatch(buf.data() + pos, msg_len, handler, stats);
        pos += msg_len;
    }

    return stats;
}

}  // namespace lob::itch
