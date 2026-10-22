# Complete terminal input walkthrough

This covers every menu option, the main matching cases, input errors, saving and restart. Follow the steps in order; the order IDs depend on that order. Do not place additional orders between these steps.

## Start with a fresh practice session

Exit any running session using 0 at the main menu. Open Terminal and enter:

```sh
cd "/path/to/OrderMatchingEngine"
make
demo_data=$(mktemp -d /tmp/dsa-demo.XXXXXX)
./matching-engine --data "$demo_data"
```

This creates a fresh practice data folder outside the project, so the first ID
is 1001 and your regular saved data stays separate. Use the same Terminal
window for the restart commands later. Keep the program open through Steps
1–21.

## How to use each block

At `Enter your choice:`, paste the block and press Enter if the final line has not been submitted. Each line answers one prompt. Read the expected result before moving to the next block. For MARKET orders, no price is entered. A blank line after menu option 6 means show all stocks' trades.

Symbols such as FIFO, PRICE, SWEEP, EMPTY and CANCEL are practice stock names. They are valid because the simulator accepts any symbol following its rules.

## Menu numbers

| Input | Action |
|---|---|
| 1 | Buy |
| 2 | Sell |
| 3 | View order book |
| 4 | Cancel |
| 5 | Modify |
| 6 | Trade history |
| 7 | Search active order |
| 8 | Statistics |
| 0 | Save and exit |

## 1. Empty book, empty history and empty statistics

```text
3
UNKNOWN
6
UNKNOWN
8
```

**Expected:** There is no book or history yet. The engine totals are all zero.

## 2. Place a buy that waits

```text
1
test
LIMIT
10
150
3
TEST
7
1001
8
```

**Expected:** Buy #1001 waits for 10 TEST shares at 150.00. Lowercase test becomes TEST. There is no seller, so best ask and spread are N/A. Search shows the waiting order.

## 3. Place a sell that does not match; show the spread

```text
2
TEST
LIMIT
10
153
3
TEST
8
```

**Expected:** Sell #1002 waits for 10 shares at 153.00. Best bid is 150.00, best ask is 153.00, and spread is 3.00. Neither price crosses the other.

## 4. Partial fill and the waiting order's price

```text
2
TEST
LIMIT
4
149
7
1001
8
```

**Expected:** Sell #1003 accepts 149.00, so it matches the waiting buy at 150.00. Four shares trade at 150.00. Buy #1001 keeps 6 shares and its old queue position.

## 5. Incoming order is larger than the available shares

```text
1
TEST
LIMIT
14
155
7
1004
3
TEST
8
```

**Expected:** Buy #1004 wants 14 shares, but only 10 are offered at 153.00. Those 10 trade at 153.00. Its remaining 4 wait at its own limit of 155.00. There are now no sellers, so spread is N/A.

## 6. Equal-price match and complete fill

```text
2
TEST
LIMIT
4
155
7
1004
3
TEST
```

**Expected:** Sell #1005 matches all 4 remaining shares of buy #1004 at exactly 155.00. Both disappear from the active book. Searching #1004 now says not active. Buy #1001 still has 6 at 150.00.

## 7. Different stocks stay separate

```text
2
TSLA
LIMIT
5
100
3
TSLA
3
TEST
```

**Expected:** Sell #1006 waits for TSLA at 100.00. It cannot match the TEST buy at 150.00 because the symbols differ.

## 8. Cancel a partially filled order; cancel again

```text
4
1001
4
1001
7
1001
6
TEST
```

**Expected:** Cancellation removes the remaining 6 shares of #1001. Its earlier 4-share trade stays in history. Cancelling or searching it again says not active.

## 9. Oldest first when prices are equal

```text
1
FIFO
LIMIT
5
100
1
FIFO
LIMIT
5
100
2
FIFO
MARKET
7
7
1008
```

**Expected:** Buys #1007 and #1008 both offer 100.00. Market sell #1009 fills the older #1007 for 5 first, then takes 2 from #1008. The second buy keeps 3.

## 10. A quantity reduction loses the old priority

```text
1
FIFO
LIMIT
5
100
5
1008
2
100
2
FIFO
MARKET
6
7
1011
```

**Expected:** New buy #1010 joins behind #1008. Reducing #1008 to 2 creates replacement #1011 at the back. Market sell #1012 therefore fills #1010 for 5 before taking 1 from #1011. Replacement #1011 has 1 left.

## 11. Increase quantity, change price, and match immediately after modification

```text
5
1011
4
101
2
FIFO
LIMIT
5
102
5
1013
6
103
7
1015
```

**Expected:** First replace #1011 with buy #1013: 4 at 101.00. Sell #1014 then waits for 5 at 102.00. Replacing the buy again with 6 at 103.00 creates #1015, immediately buys the 5 at 102.00, and leaves 1 waiting at 103.00.

## 12. A better buying price wins even when it arrived later

```text
1
PRICE
LIMIT
3
100
1
PRICE
LIMIT
3
105
2
PRICE
MARKET
4
7
1016
```

**Expected:** Buy #1016 offers 100.00. The newer #1017 offers 105.00. Market sell #1018 takes the better-priced #1017 for 3, then 1 from #1016. The older buy keeps 2.

## 13. A buyer takes the lowest ask first, across price levels

```text
2
PRICE
LIMIT
2
110
2
PRICE
LIMIT
3
108
1
PRICE
MARKET
4
3
PRICE
```

**Expected:** Sell #1019 asks 110.00. Newer sell #1020 asks only 108.00. Market buy #1021 takes the cheaper 3 first, then 1 at 110.00. Sell #1019 keeps 1.

## 14. A limit order stops before an unacceptable price

```text
2
SWEEP
LIMIT
3
100
2
SWEEP
LIMIT
3
102
2
SWEEP
LIMIT
3
105
1
SWEEP
LIMIT
8
102
3
SWEEP
8
```

**Expected:** Sells #1022, #1023 and #1024 ask 100.00, 102.00 and 105.00. Buy #1025 wants 8 at a maximum of 102.00. It buys 6 from the first two levels, then stops. Its remaining 2 wait at 102.00; all 3 shares at 105.00 remain. Spread is 3.00.

## 15. Market buy with too few sellers

```text
1
TSLA
MARKET
8
3
TSLA
```

**Expected:** Market buy #1026 wants 8 TSLA. Only 5 are waiting in sell #1006. Five trade at 100.00, and the remaining 3 are cancelled. Market orders never wait in the book.

## 16. Market sell with too few buyers

```text
2
PRICE
MARKET
5
3
PRICE
```

**Expected:** Market sell #1027 wants to sell 5. Only 2 remain in buy #1016. Those 2 trade at 100.00; the other 3 are cancelled. The waiting PRICE sell at 110.00 is unaffected.

## 17. Market orders against an empty book

```text
1
EMPTY
MARKET
5
2
EMPTY
MARKET
5
8
```

**Expected:** There are no opposite orders for either market order. Buy #1028 and sell #1029 each cancel all 5. EMPTY has no active orders or trades; its quotes and spread show N/A.

## 18. Cancel the middle, front, back and final order

```text
1
CANCEL
LIMIT
1
100
1
CANCEL
LIMIT
1
100
1
CANCEL
LIMIT
1
100
1
CANCEL
LIMIT
1
100
4
1032
4
1030
4
1033
4
1031
3
CANCEL
```

**Expected:** Four same-price buys get IDs #1030–#1033. Cancel #1032 in the middle, #1030 at the front, #1033 at the back, then the only remaining #1031. The book becomes empty.

## 19. Invalid modifications leave the original order intact

```text
5
1015
0
5
1015
2
12.345
7
1015
```

**Expected:** Zero quantity is rejected; use Cancel instead. A price with three decimal places is also rejected. Search confirms that #1015 still has 1 share at 103.00. Neither failed attempt creates an ID.

## 20. Invalid input and unknown IDs

```text
9
1
ABCDEFGHIJKLMNOP
1
TEST
STOP
1
TEST
LIMIT
0
1
TEST
LIMIT
-5
1
TEST
LIMIT
1
0
1
TEST
LIMIT
1
12.345
1
TEST
LIMIT
1
1e2
1
BAD SPACE
LIMIT
1
100
7
-1
7
999999
4
999999
5
999999
3
UNKNOWN
6
UNKNOWN
```

**Expected:** This tries a removed menu option, a 16-character symbol, unknown order type, zero/negative quantities, invalid prices, a symbol containing a space, an invalid ID and unknown IDs/symbols. Each is rejected or reports no result. No order is accepted and no ID is used. After an error, the program returns to the main menu; do not enter extra fields beyond this block.

## 21. Show one stock's history, all history, and final statistics

```text
6
FIFO
6

8
```

**Expected:** The first history view filters to FIFO. For the second view, leave the stock symbol blank by pressing Enter. The final engine totals must be 33 accepted orders, 16 trades, 57 shares traded and 4 active orders.

## 22. Save and exit

```text
0
```

**Expected:** `Orders and trades saved. Session ended.` The shell prompt returns. The practice folder now contains CURRENT and a completed snapshot with four CSV files.

## 23. Restart, prove the data returned, and cancel the newest ID

At the shell prompt in the same Terminal window:

```sh
./matching-engine --data "$demo_data"
```

Then enter:

```text
7
1015
7
1025
8
1
RESTART
LIMIT
1
100
4
1034
8
0
```

**Expected:** Startup says `Saved state loaded.` Order #1015 still has 1 remaining at 103.00. Order #1025 still has 2 remaining at 102.00. The initial totals are still 33 orders, 16 trades, 57 volume and 4 active orders. Reloading has not replayed any trades.

The next buy gets #1034, even though #1033 was cancelled earlier. Cancelling #1034 leaves 4 active orders. Accepted-order count is now 34; trades and volume stay 16 and 57. Exit saves again.

## 24. Restart again and prove cancelled IDs are not reused

At the shell prompt:

```sh
./matching-engine --data "$demo_data"
```

Then enter:

```text
1
RESTART
LIMIT
1
100
7
1035
4
1035
8
0
```

**Expected:** The new order gets #1035, not the cancelled #1034. After cancelling it, the final totals are **35 accepted orders, 16 trades, 57 shares traded and 4 active orders**. Exit saves.

## 25. Inspect the exported CSV files

At the shell prompt in the same Terminal window:

```sh
snapshot=$(printf 'snapshot-%020d' "$(cat "$demo_data/CURRENT")")
cat "$demo_data/$snapshot/orders.csv"
cat "$demo_data/$snapshot/trades.csv"
cat "$demo_data/$snapshot/symbols.csv"
cat "$demo_data/$snapshot/metadata.csv"
```

Only the following orders remain in orders.csv:

| ID | Stock | Side | Remaining | Limit price |
|---|---|---|---:|---:|
| 1015 | FIFO | BUY | 1 | 103.00 |
| 1019 | PRICE | SELL | 1 | 110.00 |
| 1024 | SWEEP | SELL | 3 | 105.00 |
| 1025 | SWEEP | BUY | 2 | 102.00 |

trades.csv has 16 trade rows whose quantities add to 57. Prices are stored in integer hundredths: 10300 means 103.00. metadata.csv stores the next order ID, 1036, and the next trade ID, 17. symbols.csv preserves accepted counts, even for symbols whose books are now empty.

Each comma separates fields. Converting C structures into these text rows is serialization. Reading them and rebuilding the book is deserialization. The program saves the current state as a snapshot; it does not need a database.

## Optional: EOF instead of menu Exit

Start the program again using the same command. At an empty `Enter your choice:` prompt, press **Control+D** on macOS. This means end of input (EOF). The program saves and exits, just as with option 0.

You can also start it, choose 1, enter a stock symbol, then press Control+D at the empty order-type prompt. An unfinished order is not submitted; the existing state is saved on exit. Keyboard shortcuts are not text to paste into an input block.

## Quick explanation for your demonstration

- Best bid: highest waiting buy price.
- Best ask: lowest waiting sell price.
- Spread: best ask minus best bid. It appears only when both sides have waiting orders.
- Limit order: obeys your price; any unmatched quantity waits.
- Market order: trades against available opposite orders; any leftover is cancelled.
- Price priority: better price first, even if that order arrived later.
- Time priority: oldest first at the same price.
- Execution price: the waiting order's price.
- Partial fill: some shares trade and some remain.
- Cancellation: removes only what is still waiting; earlier trades remain.
- Modification: cancels and replaces with a new ID and queue position.
- Statistics: accepted orders include replacements and unfilled market orders. Volume counts each traded share once. Cancellation does not reduce lifetime counts.
- Search: active orders only. Filled, cancelled and replaced IDs report not active.
- Saving: choose 0. Unsaved changes can be lost if the window is force-closed.

## Coverage and verification

All 21 input blocks, both restart sequences and the final CSV records were checked against the real executable using temporary data. The guide's four final active IDs, 16 trades and 57-share volume were verified. Normal saved data was unchanged; no test files or demo runs were added to the project.

This is a manual walkthrough of all menu features and major behavior, not every possible internal execution path. Memory allocation failures, disk failures and every possible malformed input cannot be covered by normal menu examples alone. Trade timestamps vary each time you run it.
