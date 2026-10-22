#ifndef PRICE_TREE_H
#define PRICE_TREE_H
#include "matching_types.h"
typedef struct OrderNode OrderNode;
typedef struct PriceLevel {
    Price price;
    Quantity total;
    OrderNode *head, *tail;
    struct PriceLevel *left, *right;
    int height;
} PriceLevel;
PriceLevel *price_tree_find(PriceLevel *, Price);
PriceLevel *price_tree_best(PriceLevel *, Side);
PriceLevel *price_tree_insert(PriceLevel *, PriceLevel *);
/* Removes by price without moving/copying any level or order allocations. */
PriceLevel *price_tree_remove(PriceLevel *, Price);
#endif
