#pragma once

#include "types.h"
#include <map>
#include <deque>
#include <unordered_map>
#include <vector>
#include <functional>  // std::greater
#include <optional>

// ─────────────────────────────────────────────────────────────────────────────
// OrderBook — stores all resting limit orders and matches incoming ones
//
// STRUCTURE:
//
//   BUY side (bids)                SELL side (asks)
//   ────────────────               ────────────────
//   103.00 → [Ord#3]               104.00 → [Ord#4, Ord#6]
//   102.50 → [Ord#1, Ord#2]        105.00 → [Ord#5]
//   101.00 → [Ord#7]               106.00 → [Ord#8]
//
//   Bids sorted DESCENDING  →  best bid (highest) is at map.begin()
//   Asks sorted ASCENDING   →  best ask (lowest)  is at map.begin()
//
// MATCHING RULE (price-time priority):
//   1. Best price matches first
//   2. Among orders at the same price, the EARLIEST order fills first (FIFO)
// ─────────────────────────────────────────────────────────────────────────────

class OrderBook {
public:
    // Called every time a trade is generated — plug in your own handler
    using TradeCallback = std::function<void(const Trade&)>;

    explicit OrderBook(TradeCallback on_trade = nullptr);

    // ── Public API ───────────────────────────────────────────────────────────

    // Submit a new order. Returns the assigned OrderId.
    OrderId add_order(Side side, OrderType type, double real_price, Quantity qty);

    // Cancel a resting order by ID. Returns false if ID not found.
    bool cancel_order(OrderId id);

    // Print the current state of the book to stdout (for debugging/demo)
    void print_book() const;

    // All trades generated since startup
    const std::vector<Trade>& trade_history() const { return trade_history_; }

    // Current best bid price (or nullopt if no bids)
    std::optional<double> best_bid() const;

    // Current best ask price (or nullopt if no asks)
    std::optional<double> best_ask() const;

    // A single price level in the book (price + total qty at that level)
    struct BookLevel { double price; Quantity qty; };

    // Top N bid levels, sorted best-first (highest price first)
    std::vector<BookLevel> get_bids(size_t depth = 5) const;

    // Top N ask levels, sorted best-first (lowest price first)
    std::vector<BookLevel> get_asks(size_t depth = 5) const;

private:
    // ── Internal state ───────────────────────────────────────────────────────

    // BUY side: highest price first  →  std::greater<Price> reverses default order
    std::map<Price, std::deque<Order>, std::greater<Price>> bids_;

    // SELL side: lowest price first  →  default ascending order
    std::map<Price, std::deque<Order>> asks_;

    // Fast O(1) lookup: OrderId → (Side, Price)
    // Needed so cancel_order() doesn't have to scan the whole book
    std::unordered_map<OrderId, std::pair<Side, Price>> order_index_;

    std::vector<Trade> trade_history_;

    uint64_t next_order_id_ { 1 };
    uint64_t next_trade_id_ { 1 };

    TradeCallback on_trade_;  // optional callback for each trade

    // ── Internal helpers ─────────────────────────────────────────────────────

    // Core matching loop — tries to fill `incoming` against the opposite side
    void match(Order& incoming);

    // Execute one fill between two orders, generate a Trade
    Trade execute_fill(Order& aggressor, Order& resting);

    // Remove a price level from the book if it has no more orders
    template<typename MapType>
    void cleanup_level(MapType& side, Price price);

    // Get current nanosecond timestamp
    static int64_t now_ns();
};
