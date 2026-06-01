#include "order_book.h"

#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <ctime>

using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
OrderBook::OrderBook(TradeCallback on_trade)
    : on_trade_(move(on_trade))
{}

// ─────────────────────────────────────────────────────────────────────────────
// add_order — main entry point for submitting any order
// ─────────────────────────────────────────────────────────────────────────────
OrderId OrderBook::add_order(Side side, OrderType type, double real_price, Quantity qty)
{
    if (qty == 0) throw invalid_argument("Quantity must be > 0");

    Order order {
        .id           = next_order_id_++,
        .side         = side,
        .type         = type,
        .price        = (type == OrderType::MARKET) ? 0 : to_ticks(real_price),
        .quantity     = qty,
        .orig_qty     = qty,
        .timestamp_ns = now_ns()
    };

    // Try to match against the opposite side first
    match(order);

    // If quantity remains AND it's a limit order → rest it in the book
    if (order.quantity > 0 && order.type == OrderType::LIMIT) {
        if (side == Side::BUY) {
            bids_[order.price].push_back(order);
        } else {
            asks_[order.price].push_back(order);
        }
        // Register in index for fast cancel
        order_index_[order.id] = { side, order.price };
    }
    // Market orders that couldn't be fully filled are simply discarded
    // (no partial resting — this mirrors real exchange behaviour)

    return order.id;
}

// ─────────────────────────────────────────────────────────────────────────────
// match — the heart of the engine
//
//  Walk through the opposite side, fill as much as possible.
//  Stop when:
//    (a) the incoming order is fully filled, OR
//    (b) there are no more matchable orders
// ─────────────────────────────────────────────────────────────────────────────
void OrderBook::match(Order& incoming)
{
    // Pick the opposite side of the book
    // BUY order matches against SELL side (asks), and vice versa
    if (incoming.side == Side::BUY)
    {
        // ── BUY order: walk asks from lowest price upward ──────────────────
        for (auto it = asks_.begin();
             it != asks_.end() && incoming.quantity > 0; )
        {
            Price ask_price = it->first;

            // Price check: a BUY limit order only matches if ask ≤ buy price
            // Market orders skip this check (they match at any price)
            if (incoming.type == OrderType::LIMIT && ask_price > incoming.price)
                break;  // Remaining asks are too expensive — stop

            auto& level = it->second;  // deque of orders at this price level

            while (!level.empty() && incoming.quantity > 0) {
                Order& resting = level.front();  // FIFO: always take the oldest order

                Trade trade = execute_fill(incoming, resting);
                trade_history_.push_back(trade);
                if (on_trade_) on_trade_(trade);

                if (resting.quantity == 0) {
                    order_index_.erase(resting.id);  // remove from index
                    level.pop_front();               // remove from book
                }
            }

            // Clean up empty price levels
            if (level.empty())
                it = asks_.erase(it);
            else
                ++it;
        }
    }
    else
    {
        // ── SELL order: walk bids from highest price downward ──────────────
        for (auto it = bids_.begin();
             it != bids_.end() && incoming.quantity > 0; )
        {
            Price bid_price = it->first;

            // Price check: a SELL limit order only matches if bid ≥ sell price
            if (incoming.type == OrderType::LIMIT && bid_price < incoming.price)
                break;  // Remaining bids are too low — stop

            auto& level = it->second;

            while (!level.empty() && incoming.quantity > 0) {
                Order& resting = level.front();

                Trade trade = execute_fill(incoming, resting);
                trade_history_.push_back(trade);
                if (on_trade_) on_trade_(trade);

                if (resting.quantity == 0) {
                    order_index_.erase(resting.id);
                    level.pop_front();
                }
            }

            if (level.empty())
                it = bids_.erase(it);
            else
                ++it;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// execute_fill — compute how much trades, deduct from both orders
//
//  Trade price is ALWAYS the resting order's price
//  (the aggressor pays the posted price, not their own limit)
// ─────────────────────────────────────────────────────────────────────────────
Trade OrderBook::execute_fill(Order& aggressor, Order& resting)
{
    Quantity fill_qty = min(aggressor.quantity, resting.quantity);

    aggressor.quantity -= fill_qty;
    resting.quantity   -= fill_qty;

    Trade trade {
        .trade_id      = next_trade_id_++,
        .buy_order_id  = (aggressor.side == Side::BUY) ? aggressor.id : resting.id,
        .sell_order_id = (aggressor.side == Side::SELL) ? aggressor.id : resting.id,
        .price         = resting.price,   // ← resting order's price is the execution price
        .quantity      = fill_qty,
        .timestamp_ns  = now_ns()
    };

    return trade;
}

// ─────────────────────────────────────────────────────────────────────────────
// cancel_order — remove a resting order by ID in O(1) (using the index)
// ─────────────────────────────────────────────────────────────────────────────
bool OrderBook::cancel_order(OrderId id)
{
    auto idx_it = order_index_.find(id);
    if (idx_it == order_index_.end())
        return false;  // Order not found (already filled, or bad ID)

    auto [side, price] = idx_it->second;

    // Navigate to the right price level and remove the order
    auto remove_from = [&](auto& book_side) {
        auto level_it = book_side.find(price);
        if (level_it == book_side.end()) return;

        auto& dq = level_it->second;
        for (auto it = dq.begin(); it != dq.end(); ++it) {
            if (it->id == id) {
                dq.erase(it);
                break;
            }
        }
        if (dq.empty()) book_side.erase(level_it);
    };

    if (side == Side::BUY)  remove_from(bids_);
    else                    remove_from(asks_);

    order_index_.erase(idx_it);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// best_bid / best_ask — peek at top of book
// ─────────────────────────────────────────────────────────────────────────────
optional<double> OrderBook::best_bid() const {
    if (bids_.empty()) return nullopt;
    return to_price(bids_.begin()->first);
}

optional<double> OrderBook::best_ask() const {
    if (asks_.empty()) return nullopt;
    return to_price(asks_.begin()->first);
}

vector<OrderBook::BookLevel> OrderBook::get_bids(size_t depth) const {
    vector<BookLevel> result;
    for (auto& [price, dq] : bids_) {
        Quantity total = 0;
        for (auto& o : dq) total += o.quantity;
        result.push_back({ to_price(price), total });
        if (result.size() >= depth) break;
    }
    return result;
}

vector<OrderBook::BookLevel> OrderBook::get_asks(size_t depth) const {
    vector<BookLevel> result;
    for (auto& [price, dq] : asks_) {
        Quantity total = 0;
        for (auto& o : dq) total += o.quantity;
        result.push_back({ to_price(price), total });
        if (result.size() >= depth) break;
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// print_book — pretty-print current order book state to stdout
// ─────────────────────────────────────────────────────────────────────────────
void OrderBook::print_book() const
{
    cout << "\n";
    cout << "╔══════════════════════════════════╗\n";
    cout << "║        ORDER BOOK (LOBE)         ║\n";
    cout << "╠════════════════╦═════════════════╣\n";
    cout << "║  BUY (bids)    ║  SELL (asks)    ║\n";
    cout << "╠════════════════╬═════════════════╣\n";

    // Collect up to 5 levels from each side for display
    vector<pair<double,uint64_t>> bid_levels, ask_levels;

    for (auto& [price, dq] : bids_) {
        uint64_t total = 0;
        for (auto& o : dq) total += o.quantity;
        bid_levels.push_back({ to_price(price), total });
        if (bid_levels.size() == 5) break;
    }
    for (auto& [price, dq] : asks_) {
        uint64_t total = 0;
        for (auto& o : dq) total += o.quantity;
        ask_levels.push_back({ to_price(price), total });
        if (ask_levels.size() == 5) break;
    }

    size_t rows = max(bid_levels.size(), ask_levels.size());
    if (rows == 0) {
        cout << "║   (empty)      ║   (empty)       ║\n";
    }

    for (size_t i = 0; i < rows; ++i) {
        cout << "║ ";
        if (i < bid_levels.size())
            cout << fixed << setprecision(2)
                 << setw(7) << bid_levels[i].first
                 << "  qty:" << setw(4) << bid_levels[i].second;
        else
            cout << "               ";
        cout << " ║ ";
        if (i < ask_levels.size())
            cout << fixed << setprecision(2)
                 << setw(7) << ask_levels[i].first
                 << "  qty:" << setw(4) << ask_levels[i].second;
        else
            cout << "               ";
        cout << " ║\n";
    }

    cout << "╚════════════════╩═════════════════╝\n";

    auto bid = best_bid();
    auto ask = best_ask();
    if (bid && ask)
        cout << "  Spread: " << fixed << setprecision(2)
                  << (*ask - *bid) << "\n";
    cout << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// now_ns — nanosecond timestamp using the monotonic clock
// ─────────────────────────────────────────────────────────────────────────────
int64_t OrderBook::now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}
