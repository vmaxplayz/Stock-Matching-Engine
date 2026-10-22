#include "csv_manager.h"
#include "matching_internal.h"
#include "input.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define MKDIR(p) _mkdir(p)
#else
#include <dirent.h>
#define MKDIR(p) mkdir(p, 0700)
#endif
#define PATH_CAPACITY 4096
#define LINE_CAPACITY 2048
static int is_directory(const struct stat *s) {
#ifdef _WIN32
    return (s->st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(s->st_mode);
#endif
}
static const char *order_header = "order_id,symbol,side,type,price_minor,original_quantity,remaining_quantity,filled_quantity,cancelled_quantity,sequence";
static const char *trade_header = "trade_id,buy_order_id,sell_order_id,symbol,quantity,price_minor,timestamp_utc";
static const char *meta_header = "version,generation,next_order_id,next_trade_id,next_sequence,orders_received,active_orders";
static int path_join(char *out, const char *dir, const char *name) {
    int n = snprintf(out, PATH_CAPACITY, "%s/%s", dir, name);
    return n >= 0 && n < PATH_CAPACITY;
}
static int finish_file(FILE *f) {
    int bad = ferror(f);
    if (fclose(f)) bad = 1;
    return !bad;
}
/* Reject long lines and hidden zero bytes. */
static int read_line(FILE *f, char *line) {
    size_t n = 0;
    int c, bad = 0;
    while ((c = fgetc(f)) != EOF && c != '\n') {
        if (!c || n >= LINE_CAPACITY - 1) bad = 1;
        else line[n++] = (char)c;
    }
    if (ferror(f)) return -1;
    if (c == EOF && !n && !bad) return 0;
    if (n && line[n - 1] == '\r') --n;
    line[n] = '\0'; return bad ? -1 : 1;
}
static int fields(char *line, char **parts, size_t count) {
    size_t i;
    char *s = line;
    for (i = 0; i < count; ++i) {
        char *comma;
        parts[i] = s; comma = strchr(s, ',');
        if (i + 1 == count) return *s && !comma;
        if (!comma || comma == s) return 0;
        *comma = '\0'; s = comma + 1;
    }
    return 0;
}
static int read_header(FILE *f, const char *expected) {
    char line[LINE_CAPACITY];
    return read_line(f, line) == 1 && !strcmp(line, expected);
}
static int no_more(FILE *f) { char line[LINE_CAPACITY]; return read_line(f, line) == 0; }
static int positive(const char *s, int64_t *out) { return input_parse_quantity(s, out); }
static int nonnegative(const char *s, int64_t *out) {
    uint64_t n;
    if (!input_parse_id(s, &n) || n > INT64_MAX) return 0;
    *out = (int64_t)n; return 1;
}
static int canonical_symbol(const char *s) {
    char normalized[SYMBOL_CAPACITY];
    return order_book_normalize_symbol(s, normalized) && !strcmp(s, normalized);
}
static int timestamp_valid(const char *s) {
    int y, m, d, h, min, sec, max_day;
    size_t i;
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (strlen(s) != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z') return 0;
    for (i = 0; i < 19; ++i) if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && (s[i] < '0' || s[i] > '9')) return 0;
    y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + s[3]-'0';
    m = (s[5]-'0')*10+s[6]-'0'; d = (s[8]-'0')*10+s[9]-'0';
    h = (s[11]-'0')*10+s[12]-'0'; min = (s[14]-'0')*10+s[15]-'0'; sec = (s[17]-'0')*10+s[18]-'0';
    if (!y || m < 1 || m > 12 || h > 23 || min > 59 || sec > 60) return 0;
    max_day = days[m-1];
    if (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ++max_day;
    return d > 0 && d <= max_day;
}
static int fresh_directory(const char *dir) {
    int fresh = 1;
#ifdef _WIN32
    char pattern[PATH_CAPACITY];
    WIN32_FIND_DATAA item;
    HANDLE handle;
    if (!path_join(pattern, dir, "*")) return 0;
    handle = FindFirstFileA(pattern, &item);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    do {
        if (strcmp(item.cFileName, ".") && strcmp(item.cFileName, "..") && strcmp(item.cFileName, ".gitkeep")) fresh = 0;
    } while (FindNextFileA(handle, &item));
    FindClose(handle);
#else
    DIR *d = opendir(dir);
    struct dirent *entry;
    if (!d) return 0;
    errno = 0;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..") && strcmp(entry->d_name, ".gitkeep")) fresh = 0;
    }
    if (errno) fresh = 0;
    if (closedir(d)) fresh = 0;
#endif
    return fresh;
}
static int compare_sequence(const void *a, const void *b) {
    const Order *x = a, *y = b;
    return x->sequence < y->sequence ? -1 : x->sequence > y->sequence;
}
typedef struct { Order *orders; size_t count; } OrderList;
static void copy_order(const Order *o, void *ctx) {
    OrderList *list = ctx; list->orders[list->count++] = *o;
}
MatchingStatus csv_save(const MatchingEngine *e, const char *dir) {
    char generation_dir[PATH_CAPACITY], path[PATH_CAPACITY], name[64];
    char manifest[PATH_CAPACITY], pending[PATH_CAPACITY];
    struct stat info;
    OrderList list = {0};
    uint64_t generation = 1;
    FILE *f;
    size_t i;
    if (!e || !dir || !*dir) return ME_INVALID_ARGUMENT;
    if (!matching_engine_validate(e)) return ME_CORRUPT_DATA;
    if (e->active_count > SIZE_MAX / sizeof(Order)) return ME_OVERFLOW;
    if (e->active_count) {
        list.orders = malloc(e->active_count * sizeof(Order));
        if (!list.orders) return ME_ALLOCATION_FAILED;
    }
    for (i = 0; i < SYMBOL_BUCKETS; ++i) {
        OrderBook *b;
        for (b = e->symbols[i]; b; b = b->next) (void)matching_engine_visit_book(e, b->symbol, copy_order, &list);
    }
    if (list.count > 1) qsort(list.orders, list.count, sizeof(Order), compare_sequence);
    if (stat(dir, &info)) {
        if (errno != ENOENT || MKDIR(dir)) goto save_error;
    } else if (!is_directory(&info)) goto save_error;
    if (!path_join(manifest, dir, "CURRENT") || !path_join(pending, dir, "CURRENT.tmp")) goto save_error;
    for (;;) {
        (void)snprintf(name, sizeof(name), "snapshot-%020" PRIu64, generation);
        if (!path_join(generation_dir, dir, name)) goto save_error;
        if (!MKDIR(generation_dir)) break;
        if (errno != EEXIST || generation == UINT64_MAX) goto save_error;
        ++generation;
    }
    if (!path_join(path, generation_dir, "metadata.csv") || !(f = fopen(path, "wb"))) goto save_error;
    fprintf(f, "%s\n1,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%zu\n", meta_header,
            generation, e->next_order_id, e->next_trade_id, e->next_sequence, e->orders_received, e->active_count);
    if (!finish_file(f)) goto save_error;
    if (!path_join(path, generation_dir, "symbols.csv") || !(f = fopen(path, "wb"))) goto save_error;
    fputs("symbol,orders_received\n", f);
    for (i = 0; i < SYMBOL_BUCKETS; ++i) {
        OrderBook *b;
        for (b = e->symbols[i]; b; b = b->next) fprintf(f, "%s,%" PRIu64 "\n", b->symbol, b->orders_received);
    }
    if (!finish_file(f)) goto save_error;
    if (!path_join(path, generation_dir, "orders.csv") || !(f = fopen(path, "wb"))) goto save_error;
    fprintf(f, "%s\n", order_header);
    for (i = 0; i < list.count; ++i) {
        Order *o = &list.orders[i];
        fprintf(f, "%" PRIu64 ",%s,%s,LIMIT,%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRId64 ",%" PRIu64 "\n",
            o->id, o->symbol, o->side == SIDE_BUY ? "BUY" : "SELL", o->price, o->original_quantity,
            o->remaining_quantity, o->filled_quantity, o->cancelled_quantity, o->sequence);
    }
    if (!finish_file(f)) goto save_error;
    if (!path_join(path, generation_dir, "trades.csv") || !(f = fopen(path, "wb"))) goto save_error;
    fprintf(f, "%s\n", trade_header);
    for (i = 0; i < e->trade_count; ++i) {
        const Trade *t = &e->trades[i];
        fprintf(f, "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%s,%" PRId64 ",%" PRId64 ",%s\n",
            t->id, t->buy_order_id, t->sell_order_id, t->symbol, t->quantity, t->price, t->timestamp_utc);
    }
    if (!finish_file(f)) goto save_error;
    if (!(f = fopen(pending, "wb"))) goto save_error;
    fprintf(f, "%" PRIu64 "\n", generation);
    if (!finish_file(f)) goto save_error;
#ifdef _WIN32
    if (!MoveFileExA(pending, manifest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) goto save_error;
#else
    if (rename(pending, manifest)) goto save_error;
#endif
    free(list.orders); return ME_OK;
save_error:
    free(list.orders); return ME_IO_ERROR;
}
static MatchingStatus load_metadata(MatchingEngine *e, FILE *f, uint64_t generation, size_t *active) {
    char line[LINE_CAPACITY], *v[7];
    uint64_t n[7];
    size_t i;
    if (!read_header(f, meta_header) || read_line(f, line) != 1 || !fields(line, v, 7) || !no_more(f)) return ME_CORRUPT_DATA;
    for (i = 0; i < 7; ++i) if (!input_parse_id(v[i], &n[i])) return ME_CORRUPT_DATA;
    if (n[0] != 1 || n[1] != generation || n[2] < 1001 || !n[3] || n[4] != n[2] - 1000 || n[5] != n[2] - 1001 || n[6] > SIZE_MAX) return ME_CORRUPT_DATA;
    *active = (size_t)n[6];
    e->next_order_id = n[2]; e->next_trade_id = n[3]; e->next_sequence = n[4]; e->orders_received = n[5];
    return ME_OK;
}
static MatchingStatus load_symbols(MatchingEngine *e, FILE *f) {
    char line[LINE_CAPACITY], *v[2];
    uint64_t received, sum = 0;
    int read;
    MatchingStatus s;
    if (!read_header(f, "symbol,orders_received")) return ME_CORRUPT_DATA;
    while ((read = read_line(f, line)) == 1) {
        if (!fields(line, v, 2) || !canonical_symbol(v[0]) || !input_parse_id(v[1], &received) || !received || received > UINT64_MAX - sum) return ME_CORRUPT_DATA;
        s = matching_restore_book(e, v[0], received); if (s != ME_OK) return s;
        sum += received;
    }
    return read == 0 && sum == e->orders_received ? ME_OK : ME_CORRUPT_DATA;
}
static MatchingStatus load_orders(MatchingEngine *e, FILE *f) {
    char line[LINE_CAPACITY], *v[10];
    Sequence previous = 0;
    int read;
    if (!read_header(f, order_header)) return ME_CORRUPT_DATA;
    while ((read = read_line(f, line)) == 1) {
        Order o = {0};
        MatchingStatus s;
        if (!fields(line, v, 10) || !input_parse_id(v[0], &o.id) || !canonical_symbol(v[1]) ||
            (strcmp(v[2], "BUY") && strcmp(v[2], "SELL")) || strcmp(v[3], "LIMIT") ||
            !positive(v[4], &o.price) || !positive(v[5], &o.original_quantity) || !positive(v[6], &o.remaining_quantity) ||
            !nonnegative(v[7], &o.filled_quantity) || !nonnegative(v[8], &o.cancelled_quantity) || !input_parse_id(v[9], &o.sequence) ||
            o.sequence <= previous || o.sequence > UINT64_MAX - 1000 || o.id != o.sequence + 1000) return ME_CORRUPT_DATA;
        previous = o.sequence; memcpy(o.symbol, v[1], strlen(v[1]) + 1);
        o.side = !strcmp(v[2], "BUY") ? SIDE_BUY : SIDE_SELL; o.type = ORDER_LIMIT;
        o.status = o.filled_quantity ? ORDER_PARTIALLY_FILLED : ORDER_ACTIVE;
        s = matching_restore_order(e, &o); if (s != ME_OK) return s;
    }
    return read == 0 ? ME_OK : ME_CORRUPT_DATA;
}
static MatchingStatus load_trades(MatchingEngine *e, FILE *f) {
    char line[LINE_CAPACITY], *v[7];
    int read;
    if (!read_header(f, trade_header)) return ME_CORRUPT_DATA;
    while ((read = read_line(f, line)) == 1) {
        Trade t = {0};
        MatchingStatus s;
        if (!fields(line, v, 7) || !input_parse_id(v[0], &t.id) || !input_parse_id(v[1], &t.buy_order_id) ||
            !input_parse_id(v[2], &t.sell_order_id) || !canonical_symbol(v[3]) || !positive(v[4], &t.quantity) ||
            !positive(v[5], &t.price) || !timestamp_valid(v[6])) return ME_CORRUPT_DATA;
        memcpy(t.symbol, v[3], strlen(v[3]) + 1); memcpy(t.timestamp_utc, v[6], 21);
        s = matching_restore_trade(e, &t); if (s != ME_OK) return s;
    }
    return read == 0 && e->next_trade_id == (uint64_t)e->trade_count + 1 ? ME_OK : ME_CORRUPT_DATA;
}
typedef struct {
    OrderId id;
    const OrderBook *book;
    const Order *active;
    Side side;
    Quantity filled;
    Price price;
} HistoryReference;
static const OrderBook *history_book(const MatchingEngine *e, const char *symbol) {
    OrderBook *b = e->symbols[order_book_symbol_bucket(symbol)];
    while (b && strcmp(b->symbol, symbol)) b = b->next;
    return b;
}
static int compare_reference_id(const void *a, const void *b) {
    const HistoryReference *x = a, *y = b;
    return x->id < y->id ? -1 : x->id > y->id;
}
static int compare_reference_book(const void *a, const void *b) {
    const HistoryReference *x = a, *y = b;
    return strcmp(x->book->symbol, y->book->symbol);
}
static MatchingStatus history_valid(const MatchingEngine *e) {
    HistoryReference *refs;
    size_t capacity, used = 0, unique = 0, i;
    MatchingStatus status = ME_CORRUPT_DATA;
    if (e->trade_count > (SIZE_MAX-e->active_count)/2) return ME_OVERFLOW;
    capacity = e->trade_count*2 + e->active_count;
    if (!capacity) return ME_OK;
    if (capacity > SIZE_MAX/sizeof(*refs)) return ME_OVERFLOW;
    refs = e->allocator.allocate(e->allocator.context, capacity*sizeof(*refs));
    if (!refs) return ME_ALLOCATION_FAILED;
    for (i = 0; i < e->trade_count; ++i) {
        const Trade *t = &e->trades[i];
        const OrderBook *book = history_book(e, t->symbol);
        refs[used++] = (HistoryReference){t->buy_order_id, book, NULL, SIDE_BUY, t->quantity, t->price};
        refs[used++] = (HistoryReference){t->sell_order_id, book, NULL, SIDE_SELL, t->quantity, t->price};
    }
    for (i = 0; i < e->index_capacity; ++i) {
        OrderNode *n;
        for (n = e->index[i]; n; n = n->hash_next) {
            refs[used++] = (HistoryReference){n->order.id, n->book, &n->order, n->order.side, 0, 0};
        }
    }
    qsort(refs, used, sizeof(*refs), compare_reference_id);
    for (i = 0; i < used;) {
        size_t j = i;
        HistoryReference first = refs[i];
        const Order *active = NULL;
        Quantity filled = 0;
        Price lowest = INT64_MAX, highest = 0;
        while (j < used && refs[j].id == first.id) {
            if (refs[j].book != first.book || refs[j].side != first.side || refs[j].filled > INT64_MAX-filled) goto history_done;
            filled += refs[j].filled;
            if (refs[j].active) active = refs[j].active;
            if (refs[j].price) {
                if (refs[j].price < lowest) lowest = refs[j].price;
                if (refs[j].price > highest) highest = refs[j].price;
            }
            ++j;
        }
        if (active && (active->filled_quantity != filled || (filled &&
            (active->side == SIDE_BUY ? highest > active->price : lowest < active->price)))) goto history_done;
        refs[unique++] = first; i = j;
    }
    qsort(refs, unique, sizeof(*refs), compare_reference_book);
    for (i = 0; i < unique;) {
        size_t j = i+1;
        while (j < unique && refs[j].book == refs[i].book) ++j;
        if ((uint64_t)(j-i) > refs[i].book->orders_received) goto history_done;
        i = j;
    }
    status = ME_OK;
history_done:
    e->allocator.release(e->allocator.context, refs); return status;
}
MatchingStatus csv_load(MatchingEngine **target, const char *dir, int *loaded) {
    char path[PATH_CAPACITY], generation_dir[PATH_CAPACITY], name[64], line[LINE_CAPACITY];
    uint64_t generation;
    MatchingEngine *temp;
    MatchingStatus status;
    FILE *f;
    struct stat info;
    size_t i, expected_active = 0;
    const char *names[] = {"metadata.csv", "symbols.csv", "orders.csv", "trades.csv"};
    if (!target || !*target || !dir || !*dir || !loaded) return ME_INVALID_ARGUMENT;
    if (stat(dir, &info)) {
        if (errno != ENOENT) return ME_IO_ERROR;
        *loaded = 0; return ME_OK;
    }
    if (!is_directory(&info) || !path_join(path, dir, "CURRENT")) return ME_IO_ERROR;
    f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT && fresh_directory(dir)) { *loaded = 0; return ME_OK; }
        return errno == ENOENT ? ME_CORRUPT_DATA : ME_IO_ERROR;
    }
    status = read_line(f, line) == 1 && input_parse_id(line, &generation) && generation && no_more(f) ? ME_OK : ME_CORRUPT_DATA;
    if (!finish_file(f)) status = ME_IO_ERROR;
    if (status != ME_OK) return status;
    (void)snprintf(name, sizeof(name), "snapshot-%020" PRIu64, generation);
    if (!path_join(generation_dir, dir, name)) return ME_IO_ERROR;
    temp = matching_engine_create_with_allocator(&(*target)->allocator);
    if (!temp) return ME_ALLOCATION_FAILED;
    for (i = 0; i < 4; ++i) {
        if (!path_join(path, generation_dir, names[i]) || !(f = fopen(path, "rb"))) { status = ME_IO_ERROR; goto load_error; }
        if (i == 0) status = load_metadata(temp, f, generation, &expected_active);
        else if (i == 1) status = load_symbols(temp, f);
        else if (i == 2) status = load_orders(temp, f);
        else status = load_trades(temp, f);
        if (!finish_file(f)) status = ME_IO_ERROR;
        if (status != ME_OK) goto load_error;
    }
    if (temp->active_count != expected_active || !matching_engine_validate(temp)) { status = ME_CORRUPT_DATA; goto load_error; }
    status = history_valid(temp); if (status != ME_OK) goto load_error;
    matching_engine_destroy(*target); *target = temp; *loaded = 1; return ME_OK;
load_error:
    matching_engine_destroy(temp); return status;
}
