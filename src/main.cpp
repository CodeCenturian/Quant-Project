#include "order_book.h"

#include <iostream>
#include <sstream>
#include <string>
#include <iomanip>

using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// CLI helper — print a trade whenever it occurs
// ─────────────────────────────────────────────────────────────────────────────
void print_trade(const Trade& t)
{
    cout << "\n  💰 TRADE #" << t.trade_id
              << "  price=" << fixed << setprecision(2) << to_price(t.price)
              << "  qty=" << t.quantity
              << "  (buy_order=" << t.buy_order_id
              << " ← sell_order=" << t.sell_order_id << ")\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// print_help — show available commands
// ─────────────────────────────────────────────────────────────────────────────
void print_help()
{
    cout << R"(
Commands:
  buy   <price> <qty>   →  Submit a BUY  limit order  e.g. buy 102.50 100
  sell  <price> <qty>   →  Submit a SELL limit order  e.g. sell 103.00 50
  mbuy  <qty>           →  Market BUY  (fill at best ask immediately)
  msell <qty>           →  Market SELL (fill at best bid immediately)
  cancel <order_id>     →  Cancel a resting order by ID
  book                  →  Print current order book
  trades                →  Print all trades so far
  help                  →  Show this message
  quit                  →  Exit
)";
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    cout << "══════════════════════════════════\n";
    cout << "  LOBE – Limit Order Book Engine  \n";
    cout << "══════════════════════════════════\n";
    print_help();

    // Pass our print_trade callback — it fires every time a match happens
    OrderBook book(print_trade);

    string line;
    cout << "> ";

    while (getline(cin, line))
    {
        if (line.empty()) { cout << "> "; continue; }

        istringstream ss(line);
        string cmd;
        ss >> cmd;

        try {
            if (cmd == "buy") {
                double price; Quantity qty;
                ss >> price >> qty;
                OrderId id = book.add_order(Side::BUY, OrderType::LIMIT, price, qty);
                cout << "  ✓ BUY limit order submitted  id=" << id << "\n";

            } else if (cmd == "sell") {
                double price; Quantity qty;
                ss >> price >> qty;
                OrderId id = book.add_order(Side::SELL, OrderType::LIMIT, price, qty);
                cout << "  ✓ SELL limit order submitted  id=" << id << "\n";

            } else if (cmd == "mbuy") {
                Quantity qty; ss >> qty;
                OrderId id = book.add_order(Side::BUY, OrderType::MARKET, 0.0, qty);
                cout << "  ✓ Market BUY submitted  id=" << id << "\n";

            } else if (cmd == "msell") {
                Quantity qty; ss >> qty;
                OrderId id = book.add_order(Side::SELL, OrderType::MARKET, 0.0, qty);
                cout << "  ✓ Market SELL submitted  id=" << id << "\n";

            } else if (cmd == "cancel") {
                OrderId id; ss >> id;
                bool ok = book.cancel_order(id);
                cout << (ok ? "  ✓ Order cancelled\n" : "  ✗ Order not found\n");

            } else if (cmd == "book") {
                book.print_book();

            } else if (cmd == "trades") {
                const auto& trades = book.trade_history();
                if (trades.empty()) {
                    cout << "  (no trades yet)\n";
                } else {
                    for (const auto& t : trades) print_trade(t);
                }

            } else if (cmd == "help") {
                print_help();

            } else if (cmd == "quit" || cmd == "exit") {
                cout << "Goodbye.\n";
                break;

            } else {
                cout << "  Unknown command. Type 'help'.\n";
            }

        } catch (const exception& e) {
            cout << "  Error: " << e.what() << "\n";
        }

        cout << "> ";
    }

    return 0;
}
