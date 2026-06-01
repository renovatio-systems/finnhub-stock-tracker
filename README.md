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
- libcurl
- cJSON

### Ubuntu/Debian install:
sudo apt update
sudo apt install libcurl4-openssl-dev libcjson-dev build-essential

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

gcc -o stock_monitor monitor.c -lcurl -lcjson

---

## Run

./stock_monitor

or

./stock_monitor my_tickers.txt

---

## How it works

1. Reads API key from file
2. Reads ticker symbols
3. Fetches company profile + quote from Finnhub
4. Parses JSON using cJSON
5. Prints formatted table

---

## API Endpoints

/profile2
/quote

---

## License

MIT
