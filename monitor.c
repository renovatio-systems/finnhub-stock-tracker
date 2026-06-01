#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define BASE_URL "https://finnhub.io/api/v1"
#define MAX_LINE 128

struct Memory {
    char *data;
    size_t size;
};

typedef struct {
    char name[256];
    char ticker[32];
    double price;

    double cost;
    int shares;
    double gain;

    int valid;
} StockInfo;

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

    if (strlen(buffer) == 0) return NULL;

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

    if (res != CURLE_OK || http_code != 200) {
        free(chunk.data);
        return 0;
    }

    *response = chunk.data;
    return 1;
}

static int get_company_profile(const char *symbol, const char *api_key, StockInfo *info) {
    char url[1024];
    char *response = NULL;

    snprintf(url, sizeof(url),
             BASE_URL "/stock/profile2?symbol=%s&token=%s",
             symbol, api_key);

    if (!http_get(url, &response)) return 0;

    cJSON *json = cJSON_Parse(response);
    free(response);

    if (!json) return 0;

    cJSON *name = cJSON_GetObjectItem(json, "name");
    snprintf(info->name, sizeof(info->name), "%s",
             cJSON_IsString(name) ? name->valuestring : "Unknown");

    cJSON_Delete(json);
    return 1;
}

static int get_quote(const char *symbol, const char *api_key, StockInfo *info) {
    char url[1024];
    char *response = NULL;

    snprintf(url, sizeof(url),
             BASE_URL "/quote?symbol=%s&token=%s",
             symbol, api_key);

    if (!http_get(url, &response)) return 0;

    cJSON *json = cJSON_Parse(response);
    free(response);

    if (!json) return 0;

    cJSON *current = cJSON_GetObjectItem(json, "c");

    if (!cJSON_IsNumber(current) || current->valuedouble <= 0) {
        cJSON_Delete(json);
        return 0;
    }

    info->price = current->valuedouble;
    info->valid = 1;

    cJSON_Delete(json);
    return 1;
}

static void print_header(void) {
    printf("%-35s %10s %10s %10s %12s\n",
           "Company", "Cost", "Shares", "Price", "Gain");

    printf("----------------------------------------------------------------------------------------\n");
}

static void print_stock(const StockInfo *info) {
    printf("%-35.35s %10.2f %10d %10.2f %12.2f\n",
           info->name,
           info->cost,
           info->shares,
           info->price,
           info->gain);
}

int main(int argc, char *argv[]) {
    const char *ticker_file = "tickers.txt";
    const char *api_file = "api.txt";

    if (argc >= 2) {
        ticker_file = argv[1];
    }

    char *api_key = read_api_key(api_file);
    if (!api_key) {
        fprintf(stderr, "Could not read API key\n");
        return 1;
    }

    FILE *fp = fopen(ticker_file, "r");
    if (!fp) {
        perror("Could not open ticker file");
        free(api_key);
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    char line[MAX_LINE];

    print_header();

    while (fgets(line, sizeof(line), fp)) {
        trim(line);

        if (line[0] == '\0' || line[0] == '#')
            continue;

        StockInfo info;
        memset(&info, 0, sizeof(info));

        char *token = strtok(line, ",");
        if (!token) continue;
        trim(token);
        snprintf(info.ticker, sizeof(info.ticker), "%s", token);

        token = strtok(NULL, ",");
        if (!token) continue;
        info.cost = atof(token);

        token = strtok(NULL, ",");
        if (!token) continue;
        info.shares = atoi(token);

        if (!get_company_profile(info.ticker, api_key, &info)) {
            snprintf(info.name, sizeof(info.name), "%s", info.ticker);
        }

        if (!get_quote(info.ticker, api_key, &info)) {
            fprintf(stderr, "Failed quote for %s\n", info.ticker);
            continue;
        }

        info.gain = (info.price - info.cost) * info.shares;

        print_stock(&info);
    }

    fclose(fp);
    free(api_key);
    curl_global_cleanup();

    return 0;
}