#ifndef MATCHING_ENGINE_H
#define MATCHING_ENGINE_H
#include "matching_types.h"
typedef struct MatchingEngine MatchingEngine;
/* Allocators must follow the C allocation rules. */
typedef struct {
    void *context;
    void *(*allocate)(void *, size_t);
    void *(*resize)(void *, void *, size_t);
    void (*release)(void *, void *);
} MatchingAllocator;
typedef void (*OrderVisitor)(const Order *, void *);
MatchingEngine *matching_engine_create(void);
MatchingEngine *matching_engine_create_with_allocator(const MatchingAllocator *);
void matching_engine_destroy(MatchingEngine *);
/* A failed request leaves orders, trades and counters unchanged. */
MatchingStatus matching_engine_submit(MatchingEngine *, const OrderRequest *, SubmitResult *);
MatchingStatus matching_engine_find(const MatchingEngine *, OrderId, Order *);
/* Visitors must not change the engine or keep these pointers. */
MatchingStatus matching_engine_visit_book(const MatchingEngine *, const char *, OrderVisitor, void *);
size_t matching_engine_active_count(const MatchingEngine *);
size_t matching_engine_trade_count(const MatchingEngine *);
MatchingStatus matching_engine_get_trade(const MatchingEngine *, size_t, Trade *);
/* Expensive structural audit intended for tests/debugging. */
int matching_engine_validate(const MatchingEngine *);
MatchingStatus matching_engine_cancel(MatchingEngine *, OrderId, Order *);
MatchingStatus matching_engine_modify(MatchingEngine *, OrderId, Quantity, Price, SubmitResult *);
MatchingStatus matching_engine_statistics(const MatchingEngine *, const char *, MarketStatistics *);
EngineStatistics matching_engine_totals(const MatchingEngine *);
typedef void (*SymbolVisitor)(const MarketStatistics *, void *);
MatchingStatus matching_engine_visit_symbols(const MatchingEngine *, SymbolVisitor, void *);
const char *matching_status_string(MatchingStatus);
#endif
