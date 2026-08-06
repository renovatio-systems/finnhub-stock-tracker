#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define BASE_URL "https://finnhub.io/api/v1"
#define MAX_LINE 128

#define COLOR_RESET "\033[0m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"

static int use_color = 0;

struct Memory {
    char *data;
    size_t size;
};

typedef struct {
    char name[256];
    char ticker[32];
    char currency[16];
    double price;
    double change;
    double percent_change;
    double purchase_price;
    double shares;
    int valid;
} StockInfo;

typedef struct {
    char symbol[32];
    double purchase_price;
    double shares;
} TickerEntry;

typedef struct {
    TickerEntry *items;
    size_t count;
    size_t capacity;
} TickerList;

static void ticker_list_add(TickerList *list, const TickerEntry *entry) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->items = realloc(list->items, list->capacity * sizeof(TickerEntry));
    }
    list->items[list->count] = *entry;
    list->count++;
}

static void ticker_list_free(TickerList *list) {
    free(list->items);
}

static void trim(char *s) {
    char *p = s;

    while (isspace((unsigned char)*p)) p++;
    memmove(s, p, strlen(p) + 1);

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[len - 1] = '\0';
        len--;
    }
}

static int parse_ticker_line(char *line, TickerEntry *entry) {
    char *symbol = strtok(line, ",");
    char *purchase_price = strtok(NULL, ",");
    char *shares = strtok(NULL, ",");

    if (!symbol || !purchase_price || !shares) {
        return 0;
    }

    trim(symbol);
    trim(purchase_price);
    trim(shares);

    snprintf(entry->symbol, sizeof(entry->symbol), "%s", symbol);
    entry->purchase_price = atof(purchase_price);
    entry->shares = atof(shares);

    return 1;
}

static int load_tickers(const char *filename, TickerList *list) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Could not open ticker file");
        return 0;
    }

    char line[MAX_LINE];
    int line_no = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_no++;
        trim(line);

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        TickerEntry entry;
        if (!parse_ticker_line(line, &entry)) {
            fprintf(stderr,
                    "Skipping malformed line %d in %s (expected TICKER,PURCHASE_PRICE,SHARES): %s\n",
                    line_no, filename, line);
            continue;
        }

        ticker_list_add(list, &entry);
    }

    fclose(fp);
    return 1;
}

static char *read_api_key(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Unable to open api.txt");
        return NULL;
    }

    char buffer[256];

    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    trim(buffer);

    if (strlen(buffer) == 0) {
        return NULL;
    }

    char *key = malloc(strlen(buffer) + 1);
    if (!key) return NULL;

    strcpy(key, buffer);
    return key;
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    struct Memory *mem = (struct Memory *)userp;

    char *ptr = realloc(mem->data, mem->size + real_size + 1);
    if (!ptr) return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->data[mem->size] = '\0';

    return real_size;
}

typedef struct {
    size_t ticker_index;
    int is_quote;
    struct Memory mem;
} FetchRequest;

static void parse_profile_response(const char *json_str, const char *symbol, StockInfo *info) {
    cJSON *json = cJSON_Parse(json_str);
    if (!json) return;

    cJSON *name = cJSON_GetObjectItem(json, "name");
    cJSON *ticker = cJSON_GetObjectItem(json, "ticker");
    cJSON *currency = cJSON_GetObjectItem(json, "currency");

    snprintf(info->name, sizeof(info->name), "%s",
             cJSON_IsString(name) ? name->valuestring : "Unknown");

    snprintf(info->ticker, sizeof(info->ticker), "%s",
             cJSON_IsString(ticker) ? ticker->valuestring : symbol);

    snprintf(info->currency, sizeof(info->currency), "%s",
             cJSON_IsString(currency) ? currency->valuestring : "Unknown");

    cJSON_Delete(json);
}

static void parse_quote_response(const char *json_str, StockInfo *info) {
    cJSON *json = cJSON_Parse(json_str);
    if (!json) return;

    cJSON *current = cJSON_GetObjectItem(json, "c");
    cJSON *change = cJSON_GetObjectItem(json, "d");
    cJSON *percent_change = cJSON_GetObjectItem(json, "dp");

    if (cJSON_IsNumber(current) && current->valuedouble > 0) {
        info->price = current->valuedouble;
        info->change = cJSON_IsNumber(change) ? change->valuedouble : 0.0;
        info->percent_change = cJSON_IsNumber(percent_change) ? percent_change->valuedouble : 0.0;
        info->valid = 1;
    }

    cJSON_Delete(json);
}

static CURL *make_request_handle(const char *url, FetchRequest *req) {
    req->mem.data = malloc(1);
    if (!req->mem.data) return NULL;
    req->mem.data[0] = '\0';
    req->mem.size = 0;

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(req->mem.data);
        req->mem.data = NULL;
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &req->mem);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "stock-monitor-c/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_PRIVATE, req);

    return curl;
}

/* Fetches company profile + quote for every ticker concurrently over one
 * curl multi handle instead of blocking on each request in sequence. */
static void fetch_all(const TickerList *tickers, const char *api_key, StockInfo *infos) {
    if (tickers->count == 0) return;

    FetchRequest *requests = calloc(tickers->count * 2, sizeof(FetchRequest));
    CURLM *multi = curl_multi_init();

    for (size_t i = 0; i < tickers->count; i++) {
        char url[1024];
        const char *symbol = tickers->items[i].symbol;

        FetchRequest *profile_req = &requests[i * 2];
        profile_req->ticker_index = i;
        profile_req->is_quote = 0;
        snprintf(url, sizeof(url), BASE_URL "/stock/profile2?symbol=%s&token=%s", symbol, api_key);
        CURL *profile_handle = make_request_handle(url, profile_req);
        if (profile_handle) curl_multi_add_handle(multi, profile_handle);

        FetchRequest *quote_req = &requests[i * 2 + 1];
        quote_req->ticker_index = i;
        quote_req->is_quote = 1;
        snprintf(url, sizeof(url), BASE_URL "/quote?symbol=%s&token=%s", symbol, api_key);
        CURL *quote_handle = make_request_handle(url, quote_req);
        if (quote_handle) curl_multi_add_handle(multi, quote_handle);
    }

    int still_running = 0;
    curl_multi_perform(multi, &still_running);
    while (still_running) {
        curl_multi_poll(multi, NULL, 0, 1000, NULL);
        curl_multi_perform(multi, &still_running);
    }

    CURLMsg *msg;
    int msgs_left;
    while ((msg = curl_multi_info_read(multi, &msgs_left)) != NULL) {
        if (msg->msg != CURLMSG_DONE) continue;

        CURL *easy = msg->easy_handle;
        FetchRequest *req = NULL;
        curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);

        long http_code = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

        const char *symbol = tickers->items[req->ticker_index].symbol;

        if (msg->data.result == CURLE_OK && http_code == 200) {
            if (req->is_quote) {
                parse_quote_response(req->mem.data, &infos[req->ticker_index]);
            } else {
                parse_profile_response(req->mem.data, symbol, &infos[req->ticker_index]);
            }
        } else {
            fprintf(stderr, "Failed to get %s for %s\n",
                    req->is_quote ? "quote" : "company profile", symbol);
        }

        free(req->mem.data);
        curl_multi_remove_handle(multi, easy);
        curl_easy_cleanup(easy);
    }

    curl_multi_cleanup(multi);
    free(requests);
}

static void print_header(void) {
    printf("%-35s %-10s %-12s %-20s %-12s %-24s %-10s\n",
           "Company", "Ticker", "Price", "Change", "Purchase", "Gain/Loss", "Currency");
    printf("-----------------------------------------------------------------------------------------------------------------------------\n");
}

static void print_stock(const StockInfo *info) {
    char change_buf[32];
    snprintf(change_buf, sizeof(change_buf), "%+.2f (%+.2f%%)",
             info->change, info->percent_change);

    double gain_loss = (info->price - info->purchase_price) * info->shares;
    double gain_loss_percent = info->purchase_price > 0
        ? ((info->price - info->purchase_price) / info->purchase_price) * 100.0
        : 0.0;

    char gain_loss_buf[32];
    snprintf(gain_loss_buf, sizeof(gain_loss_buf), "%+.2f (%+.2f%%)",
             gain_loss, gain_loss_percent);

    const char *day_color = use_color ? (info->change >= 0 ? COLOR_GREEN : COLOR_RED) : "";
    const char *day_reset = use_color ? COLOR_RESET : "";

    const char *pos_color = use_color ? (info->price >= info->purchase_price ? COLOR_GREEN : COLOR_RED) : "";
    const char *pos_reset = use_color ? COLOR_RESET : "";

    printf("%-35.35s %-10s %-12.2f %s%-20s%s %-12.2f %s%-24s%s %-10s\n",
           info->name,
           info->ticker,
           info->price,
           day_color, change_buf, day_reset,
           info->purchase_price,
           pos_color, gain_loss_buf, pos_reset,
           info->currency);
}

static void run_once(const TickerList *tickers, const char *api_key) {
    StockInfo *infos = calloc(tickers->count, sizeof(StockInfo));

    for (size_t i = 0; i < tickers->count; i++) {
        snprintf(infos[i].name, sizeof(infos[i].name), "Unknown");
        snprintf(infos[i].ticker, sizeof(infos[i].ticker), "%s", tickers->items[i].symbol);
        snprintf(infos[i].currency, sizeof(infos[i].currency), "Unknown");
        infos[i].purchase_price = tickers->items[i].purchase_price;
        infos[i].shares = tickers->items[i].shares;
    }

    fetch_all(tickers, api_key, infos);

    print_header();
    for (size_t i = 0; i < tickers->count; i++) {
        if (infos[i].valid) {
            print_stock(&infos[i]);
        }
    }

    free(infos);
}

int main(int argc, char *argv[]) {
    const char *ticker_file = "tickers.txt";
    const char *api_file = "api.txt";
    int watch_interval = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--watch") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing value for %s\n", argv[i]);
                return 1;
            }
            watch_interval = atoi(argv[++i]);
            if (watch_interval <= 0) {
                fprintf(stderr, "Invalid watch interval: %s\n", argv[i]);
                return 1;
            }
        } else {
            ticker_file = argv[i];
        }
    }

    char *api_key = read_api_key(api_file);
    if (!api_key) {
        fprintf(stderr, "Could not read API key from %s\n", api_file);
        return 1;
    }

    TickerList tickers = {0};
    if (!load_tickers(ticker_file, &tickers)) {
        free(api_key);
        return 1;
    }

    use_color = isatty(fileno(stdout));

    curl_global_init(CURL_GLOBAL_DEFAULT);

    do {
        if (watch_interval > 0) {
            printf("\033[H\033[J");
        }

        run_once(&tickers, api_key);

        if (watch_interval > 0) {
            fflush(stdout);
            sleep((unsigned int)watch_interval);
        }
    } while (watch_interval > 0);

    ticker_list_free(&tickers);
    free(api_key);
    curl_global_cleanup();

    return 0;
}