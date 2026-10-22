#ifndef MATCHING_TYPES_H
#define MATCHING_TYPES_H
#include <stddef.h>
#include <stdint.h>
#define SYMBOL_CAPACITY 16
typedef int64_t Price; /* Integer hundredths: 150.25 = 15025. */
typedef int64_t Quantity;
typedef uint64_t OrderId;
typedef uint64_t Sequence;
typedef enum { SIDE_BUY, SIDE_SELL } Side;
typedef enum { ORDER_LIMIT, ORDER_MARKET } OrderType;
typedef enum { ORDER_ACTIVE, ORDER_PARTIALLY_FILLED, ORDER_FILLED, ORDER_CANCELLED, ORDER_REPLACED } OrderStatus;
typedef enum {
    ME_OK, ME_INVALID_ARGUMENT, ME_NOT_FOUND, ME_ALLOCATION_FAILED, ME_OVERFLOW, ME_IO_ERROR, ME_CORRUPT_DATA
} MatchingStatus;
typedef struct {
    char symbol[SYMBOL_CAPACITY];
    Side side;
    OrderType type;
    Price price; /* Must be zero for a market request. */
    Quantity quantity;
} OrderRequest;
typedef struct {
    OrderId id;
    char symbol[SYMBOL_CAPACITY];
    Side side;
    OrderType type;
    Price price;
    Quantity original_quantity, remaining_quantity, filled_quantity, cancelled_quantity;
    Sequence sequence;
    OrderStatus status;
} Order;
typedef struct {
    uint64_t id;
    OrderId buy_order_id, sell_order_id;
    char symbol[SYMBOL_CAPACITY];
    Quantity quantity;
    Price price;
    char timestamp_utc[21];
} Trade;
typedef struct {
    char symbol[SYMBOL_CAPACITY];
    uint64_t orders_received, trades_executed, executed_volume;
    Price last_price, best_bid, best_ask;
    Quantity outstanding_buys, outstanding_sells;
} MarketStatistics;
typedef struct {
    uint64_t orders_received, trades_executed, executed_volume;
    size_t active_orders;
} EngineStatistics;
typedef struct {
    Order order; /* Final incoming-order state, copied by value. */
    size_t first_trade, trade_count; /* Indices into engine trade history. */
} SubmitResult;
#endif
