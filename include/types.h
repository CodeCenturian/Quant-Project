#pragma once

#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// WHY integers for price instead of double?
//
//   double price = 1.10 + 1.10 + 1.10;
//   price == 3.30  →  FALSE in floating point!
//
//   In an order book, comparing prices must be EXACT.
//   So we store price as ticks: multiply by 100.
//   102.50  →  stored as  10250
//   103.00  →  stored as  10300
//
//   To display: divide by 100.0
// ─────────────────────────────────────────────────────────────────────────────

using Price    = int64_t;   // Price in ticks  (real_price × 100)
using Quantity = uint64_t;  // Number of shares / contracts
using OrderId  = uint64_t;  // Unique ID assigned to each order

// ── Helpers ──────────────────────────────────────────────────────────────────
inline Price   to_ticks(double real_price)  { return static_cast<Price>(real_price * 100); }
inline double  to_price(Price ticks)        { return ticks / 100.0; }

// ── Enumerations ─────────────────────────────────────────────────────────────
enum class Side {
    BUY,   // Wants to purchase — bids
    SELL   // Wants to sell    — asks
};

enum class OrderType {
    LIMIT,   // Execute at this price or better; rest in book if unmatched
    MARKET   // Execute immediately at best available price; never rests
};

// ── Core Data Structures ──────────────────────────────────────────────────────

struct Order {
    OrderId   id;
    Side      side;
    OrderType type;
    Price     price;        // 0 for MARKET orders (price doesn't matter)
    Quantity  quantity;     // Remaining quantity yet to be filled
    Quantity  orig_qty;     // Original submitted quantity (never changes)
    int64_t   timestamp_ns; // Nanosecond timestamp — used for FIFO tie-breaking
};

struct Trade {
    uint64_t trade_id;
    OrderId  buy_order_id;
    OrderId  sell_order_id;
    Price    price;         // Execution price (always the resting order's price)
    Quantity quantity;      // How many units changed hands
    int64_t  timestamp_ns;
};
