#include "order_book.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <ctime>

using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
// High-precision timer using CLOCK_MONOTONIC_RAW
// MONOTONIC_RAW is not affected by NTP adjustments — more stable than REALTIME
// ─────────────────────────────────────────────────────────────────────────────
static inline int64_t nanos() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

// ─────────────────────────────────────────────────────────────────────────────
// Percentile helper — sorts a copy of the latency vector and indexes into it
// ─────────────────────────────────────────────────────────────────────────────
double percentile(vector<int64_t> v, double p) {
    sort(v.begin(), v.end());
    size_t idx = (size_t)(p / 100.0 * (v.size() - 1));
    return (double)v[idx];
}

// ─────────────────────────────────────────────────────────────────────────────
// Print a results table for one scenario
// ─────────────────────────────────────────────────────────────────────────────
void print_stats(const string& name, const vector<int64_t>& latencies) {
    double avg = (double)accumulate(latencies.begin(), latencies.end(), 0LL)
                 / latencies.size();

    cout << "\n┌─────────────────────────────────────────┐\n";
    cout << "│  " << left << setw(40) << name << "│\n";
    cout << "├──────────────┬──────────────────────────┤\n";
    cout << "│  Ops         │ " << setw(26) << latencies.size()    << "│\n";
    cout << "│  Mean        │ " << setw(22) << fixed << setprecision(1) << avg        << " ns │\n";
    cout << "│  P50         │ " << setw(22) << percentile(latencies, 50)  << " ns │\n";
    cout << "│  P95         │ " << setw(22) << percentile(latencies, 95)  << " ns │\n";
    cout << "│  P99         │ " << setw(22) << percentile(latencies, 99)  << " ns │\n";
    cout << "│  P99.9       │ " << setw(22) << percentile(latencies, 99.9)<< " ns │\n";
    cout << "│  Max         │ " << setw(22) << *max_element(latencies.begin(), latencies.end()) << " ns │\n";
    cout << "└──────────────┴──────────────────────────┘\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// SCENARIO 1 — Insert-only
// Submit N limit orders that never cross the spread.
// Measures the raw cost of inserting into the data structure.
// ─────────────────────────────────────────────────────────────────────────────
vector<int64_t> bench_insert_only(int N) {
    OrderBook book;
    vector<int64_t> lat;
    lat.reserve(N);

    // All buys below 100, all sells above 110 → no matching ever happens
    for (int i = 0; i < N; i++) {
        double price = 100.0 - (i % 50) * 0.01;  // spreads bids over 50 levels
        int64_t t0 = nanos();
        book.add_order(Side::BUY, OrderType::LIMIT, price, 100);
        lat.push_back(nanos() - t0);
    }
    return lat;
}

// ─────────────────────────────────────────────────────────────────────────────
// SCENARIO 2 — Match-heavy
// Alternating buy/sell that always cross → every order generates a trade.
// Measures the cost of matching + trade generation.
// ─────────────────────────────────────────────────────────────────────────────
vector<int64_t> bench_match_heavy(int N) {
    OrderBook book;
    vector<int64_t> lat;
    lat.reserve(N);

    for (int i = 0; i < N; i++) {
        int64_t t0 = nanos();
        if (i % 2 == 0)
            book.add_order(Side::BUY,  OrderType::LIMIT, 100.00, 10);
        else
            book.add_order(Side::SELL, OrderType::LIMIT, 100.00, 10);
        lat.push_back(nanos() - t0);
    }
    return lat;
}

// ─────────────────────────────────────────────────────────────────────────────
// SCENARIO 3 — Realistic mix
// Simulates a realistic order flow:
//   70% limit inserts (no cross), 20% cancels, 10% market orders
// ─────────────────────────────────────────────────────────────────────────────
vector<int64_t> bench_realistic(int N) {
    OrderBook book;
    vector<int64_t> lat;
    lat.reserve(N);
    vector<OrderId> live_ids;
    live_ids.reserve(N);

    for (int i = 0; i < N; i++) {
        int roll = i % 10;  // deterministic "random" for reproducibility
        int64_t t0 = nanos();

        if (roll < 7) {
            // 70% — limit order, no cross
            double price = (i % 2 == 0)
                ? 99.0 - (i % 10) * 0.01   // buy below market
                : 101.0 + (i % 10) * 0.01; // sell above market
            Side side = (i % 2 == 0) ? Side::BUY : Side::SELL;
            OrderId id = book.add_order(side, OrderType::LIMIT, price, 50);
            live_ids.push_back(id);
        }
        else if (roll < 9 && !live_ids.empty()) {
            // 20% — cancel a random live order
            size_t idx = i % live_ids.size();
            book.cancel_order(live_ids[idx]);
            live_ids.erase(live_ids.begin() + idx);
        }
        else {
            // 10% — market order (may or may not find liquidity)
            book.add_order(Side::BUY, OrderType::MARKET, 0.0, 10);
        }

        lat.push_back(nanos() - t0);
    }
    return lat;
}

// ─────────────────────────────────────────────────────────────────────────────
// Save raw latencies to CSV for the Python report script
// ─────────────────────────────────────────────────────────────────────────────
void save_csv(const string& filename,
              const vector<int64_t>& inserts,
              const vector<int64_t>& matches,
              const vector<int64_t>& mixed)
{
    ofstream f(filename);
    f << "insert_ns,match_ns,mixed_ns\n";
    size_t rows = max({inserts.size(), matches.size(), mixed.size()});
    for (size_t i = 0; i < rows; i++) {
        f << (i < inserts.size() ? to_string(inserts[i]) : "") << ","
          << (i < matches.size() ? to_string(matches[i]) : "") << ","
          << (i < mixed.size()   ? to_string(mixed[i])   : "") << "\n";
    }
    cout << "\nLatency data saved to: " << filename << "\n";
    cout << "Run:  python3 ../benchmark/report.py " << filename << "\n";
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main() {
    const int N = 100'000;

    cout << "LOBE Latency Benchmark  (" << N << " ops per scenario)\n";
    cout << "Compiled with: -O3 -march=native\n";

    // Warm up the CPU / cache before measuring
    cout << "\nWarming up...";
    bench_insert_only(10'000);
    cout << " done\n";

    auto inserts = bench_insert_only(N);
    print_stats("Scenario 1 — Insert only (no match)", inserts);

    auto matches = bench_match_heavy(N);
    print_stats("Scenario 2 — Match heavy (every order fills)", matches);

    auto mixed = bench_realistic(N);
    print_stats("Scenario 3 — Realistic mix (70/20/10)", mixed);

    save_csv("latency_results.csv", inserts, matches, mixed);

    return 0;
}
