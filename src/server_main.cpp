#include "order_book.h"

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>

using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// Minimal JSON field extractors
//
// We control the input format (it comes from our Node.js server),
// so we don't need a full JSON parser — just reliable key extraction.
// ─────────────────────────────────────────────────────────────────────────────

// Extract a string value:  {"action":"buy",...}  →  "buy"
string jstr(const string& json, const string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == string::npos) return "";
    pos = json.find('"', json.find(':', pos)) + 1;  // jump past ":"  and opening "
    auto end = json.find('"', pos);
    return json.substr(pos, end - pos);
}

// Extract a numeric value:  {"qty":100,...}  →  100.0
double jnum(const string& json, const string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == string::npos) return 0.0;
    pos = json.find(':', pos) + 1;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    try { return stod(json.substr(pos)); }
    catch (...) { return 0.0; }
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialisers for our types
// ─────────────────────────────────────────────────────────────────────────────

string trade_to_json(const Trade& t) {
    ostringstream o;
    o << fixed << setprecision(2);
    o << "{\"type\":\"trade\""
      << ",\"trade_id\":"    << t.trade_id
      << ",\"price\":"       << to_price(t.price)
      << ",\"qty\":"         << t.quantity
      << ",\"buy_order_id\":" << t.buy_order_id
      << ",\"sell_order_id\":"<< t.sell_order_id
      << "}";
    return o.str();
}

// Emits the top-5 book levels on each side as:
// {"type":"book","bids":[[102.50,100],[101.00,200]],"asks":[[103.00,50]]}
string book_to_json(const OrderBook& book) {
    ostringstream o;
    o << fixed << setprecision(2);
    o << "{\"type\":\"book\",\"bids\":[";

    bool first = true;
    for (auto& lvl : book.get_bids(5)) {
        if (!first) o << ",";
        o << "[" << lvl.price << "," << lvl.qty << "]";
        first = false;
    }
    o << "],\"asks\":[";
    first = true;
    for (auto& lvl : book.get_asks(5)) {
        if (!first) o << ",";
        o << "[" << lvl.price << "," << lvl.qty << "]";
        first = false;
    }
    o << "]}";
    return o.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// emit — write a JSON line to stdout and flush immediately
// Node.js reads line-by-line, so flushing after every event is critical
// ─────────────────────────────────────────────────────────────────────────────
void emit(const string& json) {
    cout << json << "\n";
    cout.flush();
}

// ─────────────────────────────────────────────────────────────────────────────
// main — JSON server loop
//
// Input  (stdin,  one JSON object per line):
//   {"action":"buy",   "price":102.50, "qty":100}
//   {"action":"sell",  "price":103.00, "qty":50}
//   {"action":"mbuy",  "qty":100}
//   {"action":"msell", "qty":100}
//   {"action":"cancel","id":5}
//
// Output (stdout, one JSON object per line):
//   {"type":"trade", ...}   — emitted for each fill as it happens
//   {"type":"ack",   ...}   — confirms the order was processed
//   {"type":"book",  ...}   — current top-5 book snapshot after each order
// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    // Disable buffering so every emit reaches Node.js immediately
    ios::sync_with_stdio(false);
    cout.setf(ios::unitbuf);

    // Trade callback: fire immediately as each fill occurs during matching
    OrderBook book([](const Trade& t) {
        emit(trade_to_json(t));
    });

    string line;
    while (getline(cin, line)) {
        if (line.empty() || line[0] != '{') continue;

        string action = jstr(line, "action");

        OrderId id = 0;
        bool    ok = true;

        if (action == "buy") {
            double   price = jnum(line, "price");
            Quantity qty   = (Quantity)jnum(line, "qty");
            id = book.add_order(Side::BUY, OrderType::LIMIT, price, qty);

        } else if (action == "sell") {
            double   price = jnum(line, "price");
            Quantity qty   = (Quantity)jnum(line, "qty");
            id = book.add_order(Side::SELL, OrderType::LIMIT, price, qty);

        } else if (action == "mbuy") {
            Quantity qty = (Quantity)jnum(line, "qty");
            id = book.add_order(Side::BUY, OrderType::MARKET, 0.0, qty);

        } else if (action == "msell") {
            Quantity qty = (Quantity)jnum(line, "qty");
            id = book.add_order(Side::SELL, OrderType::MARKET, 0.0, qty);

        } else if (action == "cancel") {
            OrderId cancel_id = (OrderId)jnum(line, "id");
            ok = book.cancel_order(cancel_id);
              emit("{\"type\":\"cancel_ack\",\"id\":" + to_string(cancel_id)
                 + ",\"ok\":" + (ok ? "true" : "false") + "}");

        } else {
            emit("{\"type\":\"error\",\"msg\":\"unknown action: " + action + "\"}");
            continue;
        }

        // Acknowledge the order
        if (action != "cancel") {
            emit("{\"type\":\"ack\",\"id\":" + to_string(id) + "}");
        }

        // Always send a fresh book snapshot after every order
        emit(book_to_json(book));
    }

    return 0;
}
