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
    int valid;
} StockInfo;

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} TickerList;

static void ticker_list_add(TickerList *list, const char *ticker) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 16;
        list->items = realloc(list->items, list->capacity * sizeof(char *));
    }
    list->items[list->count] = strdup(ticker);
    list->count++;
}

static void ticker_list_free(TickerList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
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

static int load_tickers(const char *filename, TickerList *list) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Could not open ticker file");
        return 0;
    }

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        ticker_list_add(list, line);
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

static int http_get(const char *url, char **response) {
    CURL *curl;
    CURLcode res;
    long http_code = 0;

    struct Memory chunk;
    chunk.data = malloc(1);
    chunk.size = 0;

    if (!chunk.data) return 0;
    chunk.data[0] = '\0';

    curl = curl_easy_init();
    if (!curl) {
        free(chunk.data);
        return 0;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "stock-monitor-c/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
        free(chunk.data);
        return 0;
    }

    if (http_code != 200) {
        fprintf(stderr, "HTTP error: %ld\n", http_code);
        fprintf(stderr, "Response: %s\n", chunk.data);
        free(chunk.data);
        return 0;
    }

    *response = chunk.data;
    return 1;
}

static int get_company_profile(const char *symbol, const char *api_key, StockInfo *info) {
    char url[1024];
    char *response = NULL;

    snprintf(
        url,
        sizeof(url),
        BASE_URL "/stock/profile2?symbol=%s&token=%s",
        symbol,
        api_key
    );

    if (!http_get(url, &response)) return 0;

    cJSON *json = cJSON_Parse(response);
    free(response);

    if (!json) return 0;

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
    return 1;
}

static int get_quote(const char *symbol, const char *api_key, StockInfo *info) {
    char url[1024];
    char *response = NULL;

    snprintf(
        url,
        sizeof(url),
        BASE_URL "/quote?symbol=%s&token=%s",
        symbol,
        api_key
    );

    if (!http_get(url, &response)) return 0;

    cJSON *json = cJSON_Parse(response);
    free(response);

    if (!json) return 0;

    cJSON *current = cJSON_GetObjectItem(json, "c");
    cJSON *change = cJSON_GetObjectItem(json, "d");
    cJSON *percent_change = cJSON_GetObjectItem(json, "dp");

    if (!cJSON_IsNumber(current) || current->valuedouble <= 0) {
        cJSON_Delete(json);
        return 0;
    }

    info->price = current->valuedouble;
    info->change = cJSON_IsNumber(change) ? change->valuedouble : 0.0;
    info->percent_change = cJSON_IsNumber(percent_change) ? percent_change->valuedouble : 0.0;
    info->valid = 1;

    cJSON_Delete(json);
    return 1;
}

static void print_header(void) {
    printf("%-35s %-10s %-12s %-20s %-10s\n",
           "Company", "Ticker", "Price", "Change", "Currency");
    printf("-------------------------------------------------------------------------------------------\n");
}

static void print_stock(const StockInfo *info) {
    char change_buf[32];
    snprintf(change_buf, sizeof(change_buf), "%+.2f (%+.2f%%)",
             info->change, info->percent_change);

    const char *color = use_color ? (info->change >= 0 ? COLOR_GREEN : COLOR_RED) : "";
    const char *reset = use_color ? COLOR_RESET : "";

    printf("%-35.35s %-10s %-12.2f %s%-20s%s %-10s\n",
           info->name,
           info->ticker,
           info->price,
           color, change_buf, reset,
           info->currency);
}

static void run_once(const TickerList *tickers, const char *api_key) {
    print_header();

    for (size_t i = 0; i < tickers->count; i++) {
        const char *symbol = tickers->items[i];

        StockInfo info;
        memset(&info, 0, sizeof(info));

        snprintf(info.name, sizeof(info.name), "Unknown");
        snprintf(info.ticker, sizeof(info.ticker), "%s", symbol);
        snprintf(info.currency, sizeof(info.currency), "Unknown");

        if (!get_company_profile(symbol, api_key, &info)) {
            fprintf(stderr, "Failed to get company profile for %s\n", symbol);
        }

        if (!get_quote(symbol, api_key, &info)) {
            fprintf(stderr, "Failed to get quote for %s\n", symbol);
            continue;
        }

        print_stock(&info);
    }
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
            time_t now = time(NULL);
            printf("\033[H\033[J");
            printf("Refreshing every %ds - last update: %s", watch_interval, ctime(&now));
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