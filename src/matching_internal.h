#ifndef MATCHING_INTERNAL_H
#define MATCHING_INTERNAL_H
#include "matching_engine.h"
#include "order_book.h"
struct MatchingEngine {
    MatchingAllocator allocator;
    OrderBook *symbols[SYMBOL_BUCKETS];
    OrderNode **index;
    size_t index_capacity, active_count;
    Trade *trades;
    size_t trade_count, trade_capacity;
    uint64_t next_order_id, next_sequence, next_trade_id;
    uint64_t executed_volume, orders_received;
};
MatchingStatus matching_restore_book(MatchingEngine *, const char *, uint64_t);
MatchingStatus matching_restore_order(MatchingEngine *, const Order *);
MatchingStatus matching_restore_trade(MatchingEngine *, const Trade *);
MatchingEngine *matching_clone(const MatchingEngine *);
#endif
