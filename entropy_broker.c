#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <sys/stat.h>

#define SHM_FILE "/dev/shm/qshift_entropy.bin"

// --- libcurl Memory Struct ---
struct MemoryStruct {
    char *memory;
    size_t size;
};

// --- libcurl Write Callback ---
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(!ptr) {
        printf("[ FATAL ] Not enough memory (realloc returned NULL)\n");
        return 0;
    }
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

int main() {
    printf("[ BROKER ] Q-Shift Entropy Broker initializing...\n");
    curl_global_init(CURL_GLOBAL_ALL);

    // Infinite loop: Fetch entropy, pipe to RAM, sleep, repeat.
    while (1) {
        CURL *curl_handle = curl_easy_init();
        if(curl_handle) {
            struct MemoryStruct chunk;
            chunk.memory = malloc(1);
            chunk.size = 0;

            struct curl_slist *headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            // INJECT YOUR REAL API KEY HERE:
            headers = curl_slist_append(headers, "YOUR_CISCO_OUTSHIFT_API_KEY_HERE");

            curl_easy_setopt(curl_handle, CURLOPT_URL, "https://api.qrng.outshift.com/api/v1/random_numbers");
            curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl_handle, CURLOPT_CUSTOMREQUEST, "POST");
            
            const char *json_body = "{ \"encoding\": \"raw\", \"format\": \"hexadecimal\", \"bits_per_block\": 8, \"number_of_blocks\": 1000 }";
            curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_body);
            curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
            curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Q-Shift-Broker/1.0");

            // Bypass SSL for local WSL testing
            curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);

            printf("[ BROKER ] Pulling physical quantum noise from Cisco array...\n");
            CURLcode res = curl_easy_perform(curl_handle);

            if(res == CURLE_OK && chunk.size > 0) {
                // Instantly pipe the fetched JSON string into Linux Shared Memory
                int fd = open(SHM_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd >= 0) {
                    write(fd, chunk.memory, chunk.size);
                    close(fd);
                    printf("[ SUCCESS ] Piped %lu bytes of entropy to %s\n", (unsigned long)chunk.size, SHM_FILE);
                } else {
                    printf("[ ERROR ] Failed to open %s\n", SHM_FILE);
                }
            } else {
                fprintf(stderr, "[ WARNING ] Cisco API fetch failed: %s\n", curl_easy_strerror(res));
            }

            curl_easy_cleanup(curl_handle);
            free(chunk.memory);
        }
        
        // Sleep for 60 seconds before fetching a fresh quantum payload
        // This ensures your main daemon always has fresh entropy without spamming the Cisco API
        printf("[ BROKER ] Entering standby. Next fetch in 60s...\n");
        sleep(60);
    }

    curl_global_cleanup();
    return 0;
}
