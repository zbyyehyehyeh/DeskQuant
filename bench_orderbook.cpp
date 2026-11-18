// bench_orderbook.cpp —— order book 撮合 benchmark
// 编译：g++ -O2 -std=c++20 -Icore bench_orderbook.cpp -o bench_orderbook
#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>
#include "order_book.hpp"
#include "types.hpp"

using namespace std::chrono;

// 构造五档盘口（每档 volume）
static Quote makeQuote(double mid, double vol) {
    Quote q;
    q.ts = 0;
    q.last = mid;
    for (int i = 0; i < 5; ++i) {
        q.bid_p[i] = mid - 0.01 * (i + 1);
        q.bid_v[i] = vol;
        q.ask_p[i] = mid + 0.01 * (i + 1);
        q.ask_v[i] = vol;
    }
    return q;
}

// 跑 N 次 fn，统计 p50/p99/p99.9 延迟 + 吞吐
template <typename Fn>
static void bench(const char* name, int N, Fn fn) {
    std::vector<double> lat;
    lat.reserve(N);
    auto start = high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        auto t0 = high_resolution_clock::now();
        fn();
        auto t1 = high_resolution_clock::now();
        lat.push_back((double)duration_cast<nanoseconds>(t1 - t0).count());
    }
    auto end = high_resolution_clock::now();
    double total_s = duration_cast<microseconds>(end - start).count() / 1e6;
    std::sort(lat.begin(), lat.end());
    double p50 = lat[N / 2];
    double p99 = lat[(size_t)(N * 0.99)];
    double p999 = lat[(size_t)(N * 0.999)];
    printf("%-16s p50=%7.0fns  p99=%7.0fns  p99.9=%7.0fns  %8.2f M ops/s\n",
           name, p50, p99, p999, N / total_s / 1e6);
}

int main() {
    const int N = 1'000'000;

    // 市价买：吃卖单
    {
        OrderBook book;
        book.applyQuote(makeQuote(10.0, 1e9));
        bench("市价买", N, [&]() { (void)book.matchMarketBuy(1000); });
    }
    // 市价卖：吃买单
    {
        OrderBook book;
        book.applyQuote(makeQuote(10.0, 1e9));
        bench("市价卖", N, [&]() { (void)book.matchMarketSell(1000); });
    }
    // 限价买（交叉，立即吃单）
    {
        OrderBook book;
        book.applyQuote(makeQuote(10.0, 1e9));
        bench("限价买-吃单", N, [&]() { (void)book.addLimitBuy(10.5, 1000); });
    }
    // 限价买（未交叉，挂单）
    {
        OrderBook book;
        book.applyQuote(makeQuote(10.0, 1e9));
        bench("限价买-挂单", N, [&]() { (void)book.addLimitBuy(9.5, 1000); });
    }
    // 撤单：预挂 N 个单，逐个撤
    {
        OrderBook book;
        book.applyQuote(makeQuote(10.0, 1e9));
        std::vector<uint64_t> ids;
        ids.reserve(N);
        for (int i = 0; i < N; ++i) {
            uint64_t id = 0;
            book.addLimitBuy(9.5, 1000, &id);
            ids.push_back(id);
        }
        int idx = 0;
        bench("撤单", N, [&]() { book.cancelById(ids[idx++]); });
    }

    return 0;
}
