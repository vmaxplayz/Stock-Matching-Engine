#include "cli.h"
#include "input.h"
#include "order_book.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static void print_price(Price p) { printf("%" PRId64 ".%02" PRId64, p / 100, p % 100); }
/* Read one bounded line. */
static int read_line(const char *prompt, char *buf, size_t capacity) {
    size_t n = 0;
    int c, bad = 0;
    fputs(prompt, stdout); fflush(stdout);
    while ((c = getchar()) != EOF && c != '\n') {
        if (!c || n >= capacity - 1) bad = 1;
        else buf[n++] = (char)c;
    }
    if (c == EOF && !n && !bad) return 0;
    if (n && buf[n - 1] == '\r') --n;
    buf[n] = '\0';
    if (bad) { puts("Input too long or contains invalid characters. Please try again."); return -1; }
    return 1;
}
static void print_order(const Order *o, void *ctx) {
    (void)ctx;
    printf("#%" PRIu64 "  %-15s %-4s LIMIT  ", o->id, o->symbol, o->side == SIDE_BUY ? "BUY" : "SELL");
    print_price(o->price);
    printf("  original=%" PRId64 " filled=%" PRId64 " remaining=%" PRId64 " sequence=%" PRIu64 "\n",
           o->original_quantity, o->filled_quantity, o->remaining_quantity, o->sequence);
}
static void report_result(MatchingEngine *e, const SubmitResult *result) {
    size_t i;
    printf("Order #%" PRIu64 " accepted: %s %s %s quantity=%" PRId64,
           result->order.id, result->order.side == SIDE_BUY ? "BUY" : "SELL",
           result->order.symbol, result->order.type == ORDER_LIMIT ? "LIMIT" : "MARKET",
           result->order.original_quantity);
    if (result->order.type == ORDER_LIMIT) { fputs(" price=", stdout); print_price(result->order.price); }
    putchar('\n');
    for (i = 0; i < result->trade_count; ++i) {
        Trade t;
        (void)matching_engine_get_trade(e, result->first_trade + i, &t);
        printf("TRADE #%" PRIu64 " %s buy=#%" PRIu64 " sell=#%" PRIu64 " quantity=%" PRId64 " price=",
               t.id, t.symbol, t.buy_order_id, t.sell_order_id, t.quantity);
        print_price(t.price); putchar('\n');
    }
    printf("Filled=%" PRId64 "; remaining=%" PRId64 "; cancelled=%" PRId64 ". %s\n",
           result->order.filled_quantity, result->order.remaining_quantity, result->order.cancelled_quantity,
           result->order.remaining_quantity ? "Remainder is resting." :
           result->order.cancelled_quantity ? "Unfilled market quantity cancelled." : "Order filled.");
}
static int place(MatchingEngine *e, Side side) {
    OrderRequest r = {0};
    SubmitResult result;
    MatchingStatus status;
    char buf[128];
    int input;
    r.side = side;
    input = read_line("Stock symbol: ", buf, sizeof(buf));
    if (input != 1) return input != 0;
    if (strlen(buf) >= SYMBOL_CAPACITY) { puts("Symbol must have 1-15 characters."); return 1; }
    memcpy(r.symbol, buf, strlen(buf) + 1);
    input = read_line("Order type (LIMIT/MARKET): ", buf, sizeof(buf));
    if (input != 1) return input != 0;
    if (!strcmp(buf, "LIMIT") || !strcmp(buf, "limit")) r.type = ORDER_LIMIT;
    else if (!strcmp(buf, "MARKET") || !strcmp(buf, "market")) r.type = ORDER_MARKET;
    else { puts("Use LIMIT or MARKET."); return 1; }
    input = read_line("Quantity (whole shares): ", buf, sizeof(buf));
    if (input != 1) return input != 0;
    if (!input_parse_quantity(buf, &r.quantity)) { puts("Invalid positive whole quantity."); return 1; }
    if (r.type == ORDER_LIMIT) {
        input = read_line("Limit price (e.g. 150.25): ", buf, sizeof(buf));
        if (input != 1) return input != 0;
        if (!input_parse_price(buf, &r.price)) { puts("Invalid price. Use a positive number with up to two decimals."); return 1; }
    }
    status = matching_engine_submit(e, &r, &result);
    if (status != ME_OK) { puts(matching_status_string(status)); return 1; }
    report_result(e, &result);
    return 1;
}

static int read_id(OrderId *id) {
    char line[128];
    int input = read_line("Order ID: ", line, sizeof(line));
    if (input != 1) return input;
    if (!input_parse_id(line, id) || !*id) { puts("Invalid order ID."); return -1; }
    return 1;
}
static void print_trade(const Trade *t) {
    printf("Trade #%" PRIu64 " %s %s buy=#%" PRIu64 " sell=#%" PRIu64 " quantity=%" PRId64 " price=",
        t->id, t->timestamp_utc, t->symbol, t->buy_order_id, t->sell_order_id, t->quantity);
    print_price(t->price); putchar('\n');
}
static void print_stats(const MarketStatistics *s, void *ctx) {
    (void)ctx;
    printf("\n%s: orders=%" PRIu64 " trades=%" PRIu64 " volume=%" PRIu64 "\n", s->symbol, s->orders_received, s->trades_executed, s->executed_volume);
    fputs("Last price: ", stdout); if (s->last_price) print_price(s->last_price); else fputs("N/A", stdout);
    fputs(" | Best bid: ", stdout); if (s->best_bid) print_price(s->best_bid); else fputs("N/A", stdout);
    fputs(" | Best ask: ", stdout); if (s->best_ask) print_price(s->best_ask); else fputs("N/A", stdout);
    fputs(" | Spread: ", stdout); if (s->best_bid && s->best_ask) print_price(s->best_ask-s->best_bid); else fputs("N/A", stdout);
    printf("\nOutstanding buys=%" PRId64 " sells=%" PRId64 "\n", s->outstanding_buys, s->outstanding_sells);
}
static int manage(MatchingEngine *e, int operation) {
    OrderId id;
    Order o;
    SubmitResult result;
    MatchingStatus status;
    char line[128];
    Quantity quantity;
    Price price;
    int input = read_id(&id);
    if (input != 1) return input != 0;
    if (operation == 4) {
        status = matching_engine_cancel(e, id, &o);
        if (status == ME_OK) printf("Order #%" PRIu64 " cancelled; cancelled quantity=%" PRId64 ". Earlier trades are kept.\n", id, o.cancelled_quantity);
    } else if (operation == 5) {
        status = matching_engine_find(e, id, &o);
        if (status != ME_OK) { puts(matching_status_string(status)); return 1; }
        input = read_line("New remaining quantity: ", line, sizeof(line));
        if (input != 1) return input != 0;
        if (!input_parse_quantity(line, &quantity)) { puts("Invalid positive whole quantity. Use Cancel for zero."); return 1; }
        input = read_line("New limit price: ", line, sizeof(line));
        if (input != 1) return input != 0;
        if (!input_parse_price(line, &price)) { puts("Invalid price."); return 1; }
        status = matching_engine_modify(e, id, quantity, price, &result);
        if (status == ME_OK) {
            printf("Order #%" PRIu64 " replaced by #%" PRIu64 "; time priority reset.\n", id, result.order.id);
            report_result(e, &result);
        }
    } else {
        status = matching_engine_find(e, id, &o);
        if (status == ME_OK) print_order(&o, NULL);
    }
    if (status != ME_OK) puts(matching_status_string(status));
    return 1;
}
int cli_run(MatchingEngine *e) {
    char line[128];
    int running = 1;
    puts("STOCK EXCHANGE MATCHING ENGINE\nOrders and trades are saved on clean exit, including EOF. An interrupted run can lose this session's changes.");
    while (running) {
        int input;
        puts("\n1. Place Buy Order\n2. Place Sell Order\n3. View Order Book\n4. Cancel Order\n5. Modify Order\n6. View Trade History\n7. Search Order by ID\n8. View Market Statistics\n0. Exit");
        input = read_line("Enter your choice: ", line, sizeof(line));
        if (!input) break;
        if (input < 0) continue;
        if (!strcmp(line, "0")) break;
        if (!strcmp(line, "1")) running = place(e, SIDE_BUY);
        else if (!strcmp(line, "2")) running = place(e, SIDE_SELL);
        else if (!strcmp(line, "3")) {
            MatchingStatus s;
            input = read_line("Stock symbol: ", line, sizeof(line));
            if (!input) break;
            if (input < 0) continue;
            s = matching_engine_visit_book(e, line, print_order, NULL);
            if (s != ME_OK) puts(matching_status_string(s)); else puts("End of active order book.");
        } else if (!strcmp(line, "4")) running = manage(e, 4);
        else if (!strcmp(line, "5")) running = manage(e, 5);
        else if (!strcmp(line, "7")) running = manage(e, 7);
        else if (!strcmp(line, "6")) {
            char symbol[SYMBOL_CAPACITY];
            size_t i, shown = 0;
            input = read_line("Stock symbol (blank for all): ", line, sizeof(line));
            if (!input) break;
            if (input < 0) continue;
            if (*line && !order_book_normalize_symbol(line, symbol)) { puts("Invalid symbol."); continue; }
            for (i = 0; i < matching_engine_trade_count(e); ++i) {
                Trade t;
                (void)matching_engine_get_trade(e, i, &t);
                if (!*line || !strcmp(symbol, t.symbol)) { print_trade(&t); ++shown; }
            }
            if (!shown) puts("No trades found.");
        } else if (!strcmp(line, "8")) {
            EngineStatistics totals = matching_engine_totals(e);
            printf("Engine totals: orders=%" PRIu64 " trades=%" PRIu64 " volume=%" PRIu64 " active=%zu\n",
                totals.orders_received, totals.trades_executed, totals.executed_volume, totals.active_orders);
            (void)matching_engine_visit_symbols(e, print_stats, NULL);
        } else puts("Invalid choice. Choose 0 through 8.");
    }
    return ferror(stdin) || ferror(stdout) ? 1 : 0;
}
