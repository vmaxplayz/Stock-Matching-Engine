#include "order_book.h"
#include <string.h>
int order_book_normalize_symbol(const char *s, char out[SYMBOL_CAPACITY]) {
    size_t i;

    if (!s) return 0;
    for (i = 0; i < SYMBOL_CAPACITY; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!c) {
            if (!i) return 0;
            out[i] = '\0';
            return 1;
        }
        if (i == SYMBOL_CAPACITY - 1) return 0;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-')) {
            return 0;
        }
        out[i] = (char)c;
    }

    return 0;
}

size_t order_book_symbol_bucket(const char *s) {
    size_t h = 0;

    while (*s) h = (h * 31u + (unsigned char)*s++) % SYMBOL_BUCKETS;
    return h;
}

void order_book_append(OrderBook *book, PriceLevel *level, OrderNode *node) {
    node->book = book;
    node->level = level;
    node->prev = level->tail;
    node->next = NULL;
    if (level->tail) {
        level->tail->next = node;
    } else {
        level->head = node;
    }
    level->tail = node;
    level->total += node->order.remaining_quantity;
    if (node->order.side == SIDE_BUY) {
        book->buy_total += node->order.remaining_quantity;
    } else {
        book->sell_total += node->order.remaining_quantity;
    }
}

void order_book_unlink(OrderNode *node) {
    PriceLevel *level = node->level;

    if (node->prev) {
        node->prev->next = node->next;
    } else {
        level->head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        level->tail = node->prev;
    }

    node->prev = node->next = NULL;
}
