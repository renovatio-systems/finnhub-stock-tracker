# Stock Monitor (C + Finnhub API)

A lightweight command-line stock monitoring tool written in C that fetches real-time stock data using the Finnhub API.  
It reads a list of stock tickers from a file, retrieves company profile data and live quotes, and prints a formatted table in the terminal.

---

## Features

- Fetches real-time stock prices
- Retrieves company profile information (name, ticker, currency)
- Reads tickers from a file (`tickers.txt` by default)
- Simple and clean terminal output
- Uses `libcurl` for HTTP requests
- Uses `cJSON` for JSON parsing

---

## Example Output

Company                             Ticker     Price        Currency<br>
Apple Inc                           AAPL       189.34       USD<br>
Microsoft Corporation               MSFT       415.12       USD<br>

---

## Requirements

- GCC or any C compiler
- CMake and Make
- OpenSSL development headers (used as curl's TLS backend)
- libcurl and cJSON — vendored as git submodules under `third_party/` and built from source, so no system install of either is needed

### Ubuntu/Debian install:
sudo apt update
sudo apt install build-essential cmake libssl-dev

---

## Setup

### 1. Create API key file
api.txt

Add:
YOUR_FINNHUB_API_KEY

Get key: https://finnhub.io/

---

### 2. Create ticker list
tickers.txt

Example:<br>
AAPL<br>
MSFT<br>
GOOGL<br>
TSLA<br>
AMZN<br>

---

## Build

### 1. Fetch the vendored dependencies (first time only)
git submodule update --init --recursive

### 2. Configure and build
cmake -S . -B build
cmake --build build -j"$(nproc)"

This builds `libcurl` and `cJSON` as static libraries from the vendored
submodules (curl is trimmed to OpenSSL/HTTP(S) only — no brotli, zstd,
nghttp2, or libssh2) and links them into `build/stock_monitor`.

---

## Run

`tickers.txt` is copied into `build/` automatically by CMake. Copy your
`api.txt` (see Setup above) into `build/` as well, since it holds a secret
and is intentionally not tracked or copied by the build:

./build/stock_monitor

or

./build/stock_monitor my_tickers.txt

---

## How it works

1. Reads API key from file
2. Reads ticker symbols
3. Fetches company profile + quote from Finnhub
4. Parses JSON using cJSON
5. Prints formatted table

---

## API Endpoints

/profile2<br>
/quote<br>

---

## License

MIT
