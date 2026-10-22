# Stock Matching Engine

A self-contained C17 command-line project that models a stock exchange order
book. It is designed for learning data structures, matching algorithms, and
durable file-backed state.

Orders are matched by price-time priority, trades are recorded, and the
current state is saved to versioned CSV snapshots.

This is an educational project. It does not connect to a market and does not
handle real money or shares.

## Quick start

```sh
make
./matching-engine
```

On Windows, double-click `Run Engine Windows.bat`, or run the same commands
from an MSYS2 terminal. Use `matching-engine --help` for the command-line
summary.

## Features

- Buy and sell limit orders
- Buy and sell market orders
- Price-time matching with FIFO priority at each price
- Separate order books for each stock symbol
- Cancel and modify active orders
- Trade history and market statistics
- Crash-resistant, versioned CSV persistence
- Optional custom allocator support in the engine API

## Menu

| Option | Operation |
| ---: | --- |
| 1 | Buy an order |
| 2 | Sell an order |
| 3 | View an order book |
| 4 | Cancel an active order |
| 5 | Modify an active order |
| 6 | View trade history |
| 7 | Search for an active order |
| 8 | View market statistics |
| 0 | Save and exit |

## Requirements

- A C17 compiler
- `make` for the Makefile workflow

On Windows, [MSYS2](https://www.msys2.org/) is the easiest way to install GCC.
From an MSYS2 UCRT64 terminal:

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc make
```

## Build and run

### macOS and Linux

From the project directory:

```sh
make
./matching-engine
```

You can also run `Run Engine.command` on macOS.

### Windows

Double-click `Run Engine Windows.bat`, or build from an MSYS2 terminal:

```sh
make
./matching-engine.exe
```

The Windows launcher checks for GCC on `PATH` and at
`C:\msys64\ucrt64\bin`. If GCC is installed elsewhere, add its `bin`
directory to `PATH`.

To use a different data directory:

```sh
./matching-engine --data path/to/data
```

Use `matching-engine --help` for the command-line summary. Select **0** in
the menu to save and exit cleanly. An interrupted session may lose changes
made since the last save.

## Matching rules

1. Orders are matched against the best available opposing price.
2. At the same price, older orders are filled first.
3. A limit order rests on the book when it cannot be filled immediately.
4. Unfilled market-order quantity is cancelled.
5. Modifying an order creates a replacement with a new ID and queue position.
6. Orders for different symbols never interact.

Prices are stored as integer hundredths, so `150.25` is stored as `15025`.

## Persistence

The default `data/` directory contains the latest saved snapshot. Each save
creates a new `snapshot-*` directory and then updates `data/CURRENT`. This
means an incomplete save does not replace the last complete snapshot.

The data directory is runtime state and should usually not be committed to a
shared repository. Pass `--data` with a temporary directory when trying the
engine without changing the included sample data.

## Project layout

```text
include/                  Public headers
src/                      Engine, order book, persistence, and CLI code
data/                     Sample saved state
DEMO_INPUTS.md            Guided end-to-end terminal walkthrough
Makefile                  macOS/Linux build
Run Engine.command        macOS build-and-run launcher
Run Engine Windows.bat    Windows build-and-run launcher
```

## Demo walkthrough

`DEMO_INPUTS.md` covers every menu option, including matching, partial fills,
FIFO behavior, cancellation, modification, persistence, and input errors.

## License

No license has been selected for this project yet.
