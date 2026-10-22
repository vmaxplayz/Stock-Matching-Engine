#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H
#include "price_tree.h"
#define SYMBOL_BUCKETS 64u
typedef struct OrderBook {
    char symbol[SYMBOL_CAPACITY];
    PriceLevel *bids, *asks;
    Quantity buy_total, sell_total;
    uint64_t orders_received;
    struct OrderBook *next;
} OrderBook;
struct OrderNode {
    Order order;
    OrderNode *prev, *next, *hash_next;
    PriceLevel *level;
    OrderBook *book;
};
void order_book_append(OrderBook *, PriceLevel *, OrderNode *);
void order_book_unlink(OrderNode *);
int order_book_normalize_symbol(const char *, char out[SYMBOL_CAPACITY]);
size_t order_book_symbol_bucket(const char *);
#endif
