#include "matching_internal.h"
#include <time.h>
#include "order_book.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
static void *std_allocate(void *ctx, size_t n) { (void)ctx; return malloc(n); }
static void *std_resize(void *ctx, void *p, size_t n) { (void)ctx; return realloc(p, n); }
static void std_release(void *ctx, void *p) { (void)ctx; free(p); }
static void *allocate(MatchingEngine *e, size_t n) {
    void *p = e->allocator.allocate(e->allocator.context, n);
    if (p) memset(p, 0, n);
    return p;
}
static void release(MatchingEngine *e, void *p) {
    if (p) e->allocator.release(e->allocator.context, p);
}
MatchingEngine *matching_engine_create_with_allocator(const MatchingAllocator *a) {
    MatchingEngine *e;
    if (!a || !a->allocate || !a->resize || !a->release) return NULL;
    e = a->allocate(a->context, sizeof(*e));
    if (!e) return NULL;
    memset(e, 0, sizeof(*e)); e->allocator = *a;
    e->next_order_id = 1001; e->next_sequence = e->next_trade_id = 1;
    return e;
}
MatchingEngine *matching_engine_create(void) {
    const MatchingAllocator a = { NULL, std_allocate, std_resize, std_release };
    return matching_engine_create_with_allocator(&a);
}
static OrderBook *find_book(const MatchingEngine *e, const char *symbol) {
    OrderBook *b = e->symbols[order_book_symbol_bucket(symbol)];
    while (b && strcmp(b->symbol, symbol)) b = b->next;
    return b;
}
static void free_tree(MatchingEngine *e, PriceLevel *level) {
    OrderNode *n, *next;
    if (!level) return;
    free_tree(e, level->left); free_tree(e, level->right);
    for (n = level->head; n; n = next) { next = n->next; release(e, n); }
    release(e, level);
}
void matching_engine_destroy(MatchingEngine *e) {
    size_t i;
    MatchingAllocator a;
    if (!e) return;
    a = e->allocator;
    for (i = 0; i < SYMBOL_BUCKETS; ++i) {
        OrderBook *b = e->symbols[i];
        while (b) {
            OrderBook *next = b->next;
            free_tree(e, b->bids); free_tree(e, b->asks); release(e, b); b = next;
        }
    }
    release(e, e->index); release(e, e->trades); a.release(a.context, e);
}
static size_t id_bucket(OrderId id, size_t cap) {
    id ^= id >> 33; id *= UINT64_C(0xff51afd7ed558ccd); id ^= id >> 33;
    return (size_t)(id % cap);
}
static OrderNode *find_node(const MatchingEngine *e, OrderId id) {
    OrderNode *n;
    if (!e->index_capacity) return NULL;
    n = e->index[id_bucket(id, e->index_capacity)];
    while (n && n->order.id != id) n = n->hash_next;
    return n;
}
static void index_insert(MatchingEngine *e, OrderNode *n) {
    size_t bucket = id_bucket(n->order.id, e->index_capacity);
    n->hash_next = e->index[bucket]; e->index[bucket] = n; ++e->active_count;
}
static void index_remove(MatchingEngine *e, OrderNode *n) {
    OrderNode **link = &e->index[id_bucket(n->order.id, e->index_capacity)];
    while (*link != n) link = &(*link)->hash_next;
    *link = n->hash_next; --e->active_count;
}
static MatchingStatus reserve_index(MatchingEngine *e, size_t wanted) {
    size_t cap = e->index_capacity ? e->index_capacity : 32, i;
    OrderNode **table;
    if (wanted <= e->index_capacity / 2) return ME_OK;
    while (wanted > cap / 2) {
        if (cap > SIZE_MAX / 2) return ME_OVERFLOW;
        cap *= 2;
    }
    if (cap > SIZE_MAX / sizeof(*table)) return ME_OVERFLOW;
    table = allocate(e, cap * sizeof(*table));
    if (!table) return ME_ALLOCATION_FAILED;
    for (i = 0; i < e->index_capacity; ++i) {
        OrderNode *n = e->index[i];
        while (n) {
            OrderNode *next = n->hash_next;
            size_t bucket = id_bucket(n->order.id, cap);
            n->hash_next = table[bucket]; table[bucket] = n; n = next;
        }
    }
    release(e, e->index); e->index = table; e->index_capacity = cap;
    return ME_OK;
}
static MatchingStatus reserve_trades(MatchingEngine *e, size_t additional) {
    size_t wanted, cap;
    Trade *trades;
    if (additional > SIZE_MAX - e->trade_count) return ME_OVERFLOW;
    wanted = e->trade_count + additional;
    if (wanted <= e->trade_capacity) return ME_OK;
    cap = e->trade_capacity ? e->trade_capacity : 16;
    while (cap < wanted) {
        if (cap > SIZE_MAX / 2) { cap = wanted; break; }
        cap *= 2;
    }
    if (cap > SIZE_MAX / sizeof(*trades)) return ME_OVERFLOW;
    trades = e->allocator.resize(e->allocator.context, e->trades, cap * sizeof(*trades));
    if (!trades) return ME_ALLOCATION_FAILED;
    e->trades = trades; e->trade_capacity = cap;
    return ME_OK;
}
static int crosses(const OrderRequest *r, Price price) {
    return r->type == ORDER_MARKET || (r->side == SIDE_BUY ? r->price >= price : r->price <= price);
}
/* Count fills before changing any orders. */
static void preview(PriceLevel *root, const OrderRequest *r, Quantity *left, size_t *fills) {
    OrderNode *n;
    if (!root || !*left) return;
    preview(r->side == SIDE_BUY ? root->left : root->right, r, left, fills);
    if (!*left || !crosses(r, root->price)) return;
    for (n = root->head; n && *left; n = n->next) {
        Quantity q = *left < n->order.remaining_quantity ? *left : n->order.remaining_quantity;
        *left -= q; ++*fills;
    }
    preview(r->side == SIDE_BUY ? root->right : root->left, r, left, fills);
}
MatchingStatus matching_engine_submit(MatchingEngine *e, const OrderRequest *r, SubmitResult *out) {
    char symbol[SYMBOL_CAPACITY];
    OrderBook *book, *new_book = NULL;
    OrderNode *incoming = NULL;
    PriceLevel *level = NULL, *new_level = NULL;
    Quantity remainder, same_total, executed;
    size_t fills = 0, i;
    MatchingStatus status;
    SubmitResult result;
    char timestamp[21] = "";
    if (!e || !r || !out || !order_book_normalize_symbol(r->symbol, symbol) ||
        (r->side != SIDE_BUY && r->side != SIDE_SELL) ||
        (r->type != ORDER_LIMIT && r->type != ORDER_MARKET) || r->quantity <= 0 ||
        (r->type == ORDER_LIMIT ? r->price <= 0 : r->price != 0)) return ME_INVALID_ARGUMENT;
    if (e->next_order_id == UINT64_MAX || e->next_sequence == UINT64_MAX) return ME_OVERFLOW;
    book = find_book(e, symbol); remainder = r->quantity;
    if (book) preview(r->side == SIDE_BUY ? book->asks : book->bids, r, &remainder, &fills);
    executed = r->quantity - remainder;
    if (e->orders_received == UINT64_MAX || (book && book->orders_received == UINT64_MAX)) return ME_OVERFLOW;
    if (fills) {
        time_t now = time(NULL);
        struct tm *utc = now == (time_t)-1 ? NULL : gmtime(&now);
        if (!utc || strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", utc) != 20) return ME_IO_ERROR;
    }
    if (fills > UINT64_MAX - e->next_trade_id || (uint64_t)executed > UINT64_MAX - e->executed_volume) return ME_OVERFLOW;
    same_total = !book ? 0 : (r->side == SIDE_BUY ? book->buy_total : book->sell_total);
    if (r->type == ORDER_LIMIT && remainder > INT64_MAX - same_total) return ME_OVERFLOW;
    if (r->type == ORDER_LIMIT && remainder) {
        if (e->active_count == SIZE_MAX) return ME_OVERFLOW;
        incoming = allocate(e, sizeof(*incoming));
        if (!incoming) { status = ME_ALLOCATION_FAILED; goto fail; }
        if (book) level = price_tree_find(r->side == SIDE_BUY ? book->bids : book->asks, r->price);
        if (!level) {
            new_level = allocate(e, sizeof(*new_level));
            if (!new_level) { status = ME_ALLOCATION_FAILED; goto fail; }
            new_level->price = r->price; new_level->height = 1; level = new_level;
        }
    }
    if (!book) {
        new_book = allocate(e, sizeof(*new_book));
        if (!new_book) { status = ME_ALLOCATION_FAILED; goto fail; }
        memcpy(new_book->symbol, symbol, strlen(symbol) + 1); book = new_book;
    }
    status = reserve_trades(e, fills);
    if (status != ME_OK) goto fail;
    if (incoming) {
        status = reserve_index(e, e->active_count + 1);
        if (status != ME_OK) goto fail;
    }
    /* All memory is ready; now apply the order. */
    memset(&result, 0, sizeof(result));
    result.order.id = e->next_order_id++; result.order.sequence = e->next_sequence++;
    memcpy(result.order.symbol, symbol, strlen(symbol) + 1);
    result.order.side = r->side; result.order.type = r->type; result.order.price = r->price;
    result.order.original_quantity = r->quantity; result.order.remaining_quantity = r->quantity;
    result.first_trade = e->trade_count; result.trade_count = fills;
    if (new_book) {
        size_t bucket = order_book_symbol_bucket(symbol);
        new_book->next = e->symbols[bucket]; e->symbols[bucket] = new_book;
    }
    ++e->orders_received; ++book->orders_received;
    for (i = 0; i < fills; ++i) {
        Side opposite = r->side == SIDE_BUY ? SIDE_SELL : SIDE_BUY;
        PriceLevel **root = r->side == SIDE_BUY ? &book->asks : &book->bids;
        PriceLevel *best = price_tree_best(*root, opposite);
        OrderNode *resting = best->head;
        Quantity q = result.order.remaining_quantity < resting->order.remaining_quantity ?
            result.order.remaining_quantity : resting->order.remaining_quantity;
        Trade *t = &e->trades[e->trade_count++];
        memset(t, 0, sizeof(*t)); memcpy(t->timestamp_utc, timestamp, sizeof(timestamp)); t->id = e->next_trade_id++;
        memcpy(t->symbol, symbol, strlen(symbol) + 1); t->quantity = q; t->price = best->price;
        t->buy_order_id = r->side == SIDE_BUY ? result.order.id : resting->order.id;
        t->sell_order_id = r->side == SIDE_SELL ? result.order.id : resting->order.id;
        result.order.remaining_quantity -= q; result.order.filled_quantity += q;
        resting->order.remaining_quantity -= q; resting->order.filled_quantity += q;
        best->total -= q; e->executed_volume += (uint64_t)q;
        if (opposite == SIDE_BUY) book->buy_total -= q; else book->sell_total -= q;
        if (!resting->order.remaining_quantity) {
            order_book_unlink(resting); index_remove(e, resting); release(e, resting);
            if (!best->head) { *root = price_tree_remove(*root, best->price); release(e, best); }
        } else resting->order.status = ORDER_PARTIALLY_FILLED;
    }
    if (r->type == ORDER_MARKET && result.order.remaining_quantity) {
        result.order.cancelled_quantity = result.order.remaining_quantity;
        result.order.remaining_quantity = 0; result.order.status = ORDER_CANCELLED;
    } else if (!result.order.remaining_quantity) result.order.status = ORDER_FILLED;
    else {
        PriceLevel **root = r->side == SIDE_BUY ? &book->bids : &book->asks;
        result.order.status = executed ? ORDER_PARTIALLY_FILLED : ORDER_ACTIVE;
        incoming->order = result.order;
        if (new_level) *root = price_tree_insert(*root, new_level);
        order_book_append(book, level, incoming); index_insert(e, incoming);
    }
    *out = result; return ME_OK;
fail:
    release(e, new_book); release(e, new_level); release(e, incoming); return status;
}
MatchingStatus matching_engine_find(const MatchingEngine *e, OrderId id, Order *out) {
    OrderNode *n;
    if (!e || !out) return ME_INVALID_ARGUMENT;
    n = find_node(e, id); if (!n) return ME_NOT_FOUND;
    *out = n->order; return ME_OK;
}
static void visit_tree(const PriceLevel *root, Side side, OrderVisitor visitor, void *ctx) {
    OrderNode *n;
    if (!root) return;
    visit_tree(side == SIDE_BUY ? root->right : root->left, side, visitor, ctx);
    for (n = root->head; n; n = n->next) visitor(&n->order, ctx);
    visit_tree(side == SIDE_BUY ? root->left : root->right, side, visitor, ctx);
}
MatchingStatus matching_engine_visit_book(const MatchingEngine *e, const char *symbol, OrderVisitor visitor, void *ctx) {
    char normalized[SYMBOL_CAPACITY];
    OrderBook *b;
    if (!e || !visitor || !order_book_normalize_symbol(symbol, normalized)) return ME_INVALID_ARGUMENT;
    b = find_book(e, normalized); if (!b) return ME_NOT_FOUND;
    visit_tree(b->bids, SIDE_BUY, visitor, ctx); visit_tree(b->asks, SIDE_SELL, visitor, ctx); return ME_OK;
}
size_t matching_engine_active_count(const MatchingEngine *e) { return e ? e->active_count : 0; }
size_t matching_engine_trade_count(const MatchingEngine *e) { return e ? e->trade_count : 0; }
MatchingStatus matching_engine_get_trade(const MatchingEngine *e, size_t i, Trade *out) {
    if (!e || !out) return ME_INVALID_ARGUMENT;
    if (i >= e->trade_count) return ME_NOT_FOUND;
    *out = e->trades[i]; return ME_OK;
}
static int audit_tree(const MatchingEngine *e, const OrderBook *b, const PriceLevel *l,
                      Side side, Price lower, Price upper, size_t *count, Quantity *total) {
    OrderNode *n, *prev = NULL;
    Quantity sum = 0;
    int lh, rh;
    Sequence seq = 0;
    if (!l) return 1;
    if (l->price <= lower || l->price > upper || !l->head || !l->tail) return 0;
    lh = l->left ? l->left->height : 0; rh = l->right ? l->right->height : 0;
    if (lh - rh > 1 || rh - lh > 1 || l->height != 1 + (lh > rh ? lh : rh)) return 0;
    if (l->left && l->left->price >= l->price) return 0;
    if (!audit_tree(e, b, l->left, side, lower, l->price - 1, count, total)) return 0;
    for (n = l->head; n; n = n->next) {
        const Order *o = &n->order;
        if (n->prev != prev || n->level != l || n->book != b || o->side != side ||
            o->type != ORDER_LIMIT || o->price != l->price || strcmp(o->symbol, b->symbol) ||
            o->remaining_quantity <= 0 || o->filled_quantity < 0 || o->cancelled_quantity != 0 ||
            o->original_quantity < o->remaining_quantity ||
            o->original_quantity - o->remaining_quantity != o->filled_quantity ||
            o->status != (o->filled_quantity ? ORDER_PARTIALLY_FILLED : ORDER_ACTIVE) ||
            o->sequence <= seq || o->sequence >= e->next_sequence || o->id >= e->next_order_id ||
            find_node(e, o->id) != n || sum > INT64_MAX - o->remaining_quantity) return 0;
        sum += o->remaining_quantity; seq = o->sequence; prev = n; ++*count;
    }
    if (prev != l->tail || sum != l->total || *total > INT64_MAX - sum) return 0;
    *total += sum;
    return audit_tree(e, b, l->right, side, l->price, upper, count, total);
}
int matching_engine_validate(const MatchingEngine *e) {
    size_t i, count = 0, indexed = 0;
    uint64_t volume = 0, received = 0;
    if (!e) return 0;
    if (e->orders_received > UINT64_MAX - 1001 || e->next_order_id != e->orders_received + 1001 ||
        e->next_sequence != e->orders_received + 1 || e->next_trade_id != (uint64_t)e->trade_count + 1) return 0;
    for (i = 0; i < SYMBOL_BUCKETS; ++i) {
        OrderBook *b;
        for (b = e->symbols[i]; b; b = b->next) {
            Quantity buys = 0, sells = 0;
            PriceLevel *bid = price_tree_best(b->bids, SIDE_BUY), *ask = price_tree_best(b->asks, SIDE_SELL);
            if (!b->orders_received || b->orders_received > UINT64_MAX - received) return 0;
            received += b->orders_received;
            if (order_book_symbol_bucket(b->symbol) != i || (bid && ask && bid->price >= ask->price) ||
                !audit_tree(e, b, b->bids, SIDE_BUY, 0, INT64_MAX, &count, &buys) ||
                !audit_tree(e, b, b->asks, SIDE_SELL, 0, INT64_MAX, &count, &sells) ||
                buys != b->buy_total || sells != b->sell_total) return 0;
        }
    }
    for (i = 0; i < e->index_capacity; ++i) {
        OrderNode *n;
        for (n = e->index[i]; n; n = n->hash_next) {
            if (id_bucket(n->order.id, e->index_capacity) != i || ++indexed > e->active_count) return 0;
        }
    }
    for (i = 0; i < e->trade_count; ++i) {
        const Trade *t = &e->trades[i];
        if (t->id != (uint64_t)i + 1 || t->quantity <= 0 || t->price <= 0 ||
            t->buy_order_id >= e->next_order_id || t->sell_order_id >= e->next_order_id ||
            t->buy_order_id == t->sell_order_id || (uint64_t)t->quantity > UINT64_MAX - volume) return 0;
        volume += (uint64_t)t->quantity;
    }
    return count == e->active_count && indexed == count && volume == e->executed_volume && received == e->orders_received;
}
const char *matching_status_string(MatchingStatus s) {
    switch (s) {
    case ME_OK: return "Success";
    case ME_INVALID_ARGUMENT: return "Invalid order or argument";
    case ME_NOT_FOUND: return "Not active / symbol not found";
    case ME_ALLOCATION_FAILED: return "Not enough memory; order was not accepted";
    case ME_OVERFLOW: return "Numeric or capacity limit exceeded; order was not accepted";
    case ME_IO_ERROR: return "File or clock error";
    case ME_CORRUPT_DATA: return "Invalid or incomplete saved data";
    }
    return "Unknown error";
}

MatchingStatus matching_engine_cancel(MatchingEngine *e, OrderId id, Order *out) {
    OrderNode *n;
    Order result;
    PriceLevel *level, **root;
    Quantity q;
    if (!e || !out) return ME_INVALID_ARGUMENT;
    n = find_node(e, id);
    if (!n) return ME_NOT_FOUND;
    result = n->order; q = result.remaining_quantity;
    result.remaining_quantity = 0; result.cancelled_quantity += q; result.status = ORDER_CANCELLED;
    level = n->level;
    root = n->order.side == SIDE_BUY ? &n->book->bids : &n->book->asks;
    level->total -= q;
    if (n->order.side == SIDE_BUY) n->book->buy_total -= q; else n->book->sell_total -= q;
    order_book_unlink(n); index_remove(e, n); release(e, n);
    if (!level->head) { *root = price_tree_remove(*root, level->price); release(e, level); }
    *out = result; return ME_OK;
}
MatchingStatus matching_restore_book(MatchingEngine *e, const char *symbol, uint64_t received) {
    char normalized[SYMBOL_CAPACITY];
    OrderBook *b;
    size_t bucket;
    if (!order_book_normalize_symbol(symbol, normalized) || strcmp(symbol, normalized) || !received || find_book(e, symbol)) return ME_CORRUPT_DATA;
    b = allocate(e, sizeof(*b));
    if (!b) return ME_ALLOCATION_FAILED;
    memcpy(b->symbol, symbol, strlen(symbol) + 1); b->orders_received = received;
    bucket = order_book_symbol_bucket(symbol); b->next = e->symbols[bucket]; e->symbols[bucket] = b;
    return ME_OK;
}
MatchingStatus matching_restore_order(MatchingEngine *e, const Order *o) {
    OrderBook *b = find_book(e, o->symbol);
    OrderNode *n;
    PriceLevel *level, *created = NULL, **root;
    Quantity total;
    MatchingStatus status;
    if (!b || o->id < 1001 || o->id >= e->next_order_id || !o->sequence || o->sequence >= e->next_sequence ||
        o->type != ORDER_LIMIT || (o->side != SIDE_BUY && o->side != SIDE_SELL) ||
        o->price <= 0 || o->remaining_quantity <= 0 || o->filled_quantity < 0 || o->cancelled_quantity != 0 ||
        o->original_quantity < o->remaining_quantity || o->original_quantity - o->remaining_quantity != o->filled_quantity || find_node(e, o->id)) return ME_CORRUPT_DATA;
    total = o->side == SIDE_BUY ? b->buy_total : b->sell_total;
    if (o->remaining_quantity > INT64_MAX - total) return ME_CORRUPT_DATA;
    root = o->side == SIDE_BUY ? &b->bids : &b->asks;
    level = price_tree_find(*root, o->price);
    if (level && level->tail->order.sequence >= o->sequence) return ME_CORRUPT_DATA;
    if (e->active_count == SIZE_MAX) return ME_OVERFLOW;
    n = allocate(e, sizeof(*n)); if (!n) return ME_ALLOCATION_FAILED;
    if (!level) {
        created = allocate(e, sizeof(*created));
        if (!created) { release(e, n); return ME_ALLOCATION_FAILED; }
        created->price = o->price; created->height = 1; level = created;
    }
    status = reserve_index(e, e->active_count + 1);
    if (status != ME_OK) { release(e, created); release(e, n); return status; }
    n->order = *o;
    if (created) *root = price_tree_insert(*root, created);
    order_book_append(b, level, n); index_insert(e, n); return ME_OK;
}
MatchingStatus matching_restore_trade(MatchingEngine *e, const Trade *t) {
    MatchingStatus status;
    if (t->id != (uint64_t)e->trade_count + 1 || t->id >= e->next_trade_id || t->quantity <= 0 || t->price <= 0 ||
        t->buy_order_id < 1001 || t->sell_order_id < 1001 || t->buy_order_id >= e->next_order_id ||
        t->sell_order_id >= e->next_order_id || t->buy_order_id == t->sell_order_id || !find_book(e, t->symbol) ||
        (uint64_t)t->quantity > UINT64_MAX - e->executed_volume) return ME_CORRUPT_DATA;
    status = reserve_trades(e, 1); if (status != ME_OK) return status;
    e->trades[e->trade_count++] = *t; e->executed_volume += (uint64_t)t->quantity; return ME_OK;
}
static int clone_tree(MatchingEngine *copy, const PriceLevel *root) {
    OrderNode *n;
    if (!root) return 1;
    if (!clone_tree(copy, root->left)) return 0;
    for (n = root->head; n; n = n->next) if (matching_restore_order(copy, &n->order) != ME_OK) return 0;
    return clone_tree(copy, root->right);
}
MatchingEngine *matching_clone(const MatchingEngine *e) {
    MatchingEngine *copy = matching_engine_create_with_allocator(&e->allocator);
    size_t i;
    if (!copy) return NULL;
    copy->next_order_id = e->next_order_id; copy->next_sequence = e->next_sequence;
    copy->next_trade_id = e->next_trade_id; copy->orders_received = e->orders_received;
    for (i = 0; i < SYMBOL_BUCKETS; ++i) {
        OrderBook *b;
        for (b = e->symbols[i]; b; b = b->next) {
            if (matching_restore_book(copy, b->symbol, b->orders_received) != ME_OK ||
                !clone_tree(copy, b->bids) || !clone_tree(copy, b->asks)) goto fail_clone;
        }
    }
    for (i = 0; i < e->trade_count; ++i) if (matching_restore_trade(copy, &e->trades[i]) != ME_OK) goto fail_clone;
    return copy;
fail_clone:
    matching_engine_destroy(copy); return NULL;
}
MatchingStatus matching_engine_modify(MatchingEngine *e, OrderId id, Quantity quantity, Price price, SubmitResult *out) {
    OrderNode *original;
    MatchingEngine *copy;
    MatchingEngine old;
    Order cancelled;
    OrderRequest request = {0};
    SubmitResult result;
    MatchingStatus status;
    if (!e || !out || quantity <= 0 || price <= 0) return ME_INVALID_ARGUMENT;
    original = find_node(e, id); if (!original) return ME_NOT_FOUND;
    memcpy(request.symbol, original->order.symbol, sizeof(request.symbol));
    request.side = original->order.side; request.type = ORDER_LIMIT; request.price = price; request.quantity = quantity;
    /* Try the change on a copy so failure keeps the original order. */
    copy = matching_clone(e); if (!copy) return ME_ALLOCATION_FAILED;
    status = matching_engine_cancel(copy, id, &cancelled);
    if (status == ME_OK) status = matching_engine_submit(copy, &request, &result);
    if (status != ME_OK) { matching_engine_destroy(copy); return status; }
    old = *e; *e = *copy; *copy = old;
    matching_engine_destroy(copy); *out = result; return ME_OK;
}
MatchingStatus matching_engine_statistics(const MatchingEngine *e, const char *symbol, MarketStatistics *out) {
    char normalized[SYMBOL_CAPACITY];
    MarketStatistics s = {0};
    OrderBook *b;
    PriceLevel *best;
    size_t i;
    if (!e || !out || !order_book_normalize_symbol(symbol, normalized)) return ME_INVALID_ARGUMENT;
    b = find_book(e, normalized); if (!b) return ME_NOT_FOUND;
    memcpy(s.symbol, b->symbol, sizeof(s.symbol)); s.orders_received = b->orders_received;
    s.outstanding_buys = b->buy_total; s.outstanding_sells = b->sell_total;
    best = price_tree_best(b->bids, SIDE_BUY); if (best) s.best_bid = best->price;
    best = price_tree_best(b->asks, SIDE_SELL); if (best) s.best_ask = best->price;
    for (i = 0; i < e->trade_count; ++i) if (!strcmp(e->trades[i].symbol, b->symbol)) {
        ++s.trades_executed; s.executed_volume += (uint64_t)e->trades[i].quantity; s.last_price = e->trades[i].price;
    }
    *out = s; return ME_OK;
}
EngineStatistics matching_engine_totals(const MatchingEngine *e) {
    EngineStatistics s = {0};
    if (e) {
        s.orders_received = e->orders_received; s.trades_executed = (uint64_t)e->trade_count;
        s.executed_volume = e->executed_volume; s.active_orders = e->active_count;
    }
    return s;
}
MatchingStatus matching_engine_visit_symbols(const MatchingEngine *e, SymbolVisitor visitor, void *ctx) {
    size_t i;
    if (!e || !visitor) return ME_INVALID_ARGUMENT;
    for (i = 0; i < SYMBOL_BUCKETS; ++i) {
        OrderBook *b;
        for (b = e->symbols[i]; b; b = b->next) {
            MarketStatistics s;
            (void)matching_engine_statistics(e, b->symbol, &s); visitor(&s, ctx);
        }
    }
    return ME_OK;
}
