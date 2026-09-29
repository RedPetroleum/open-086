// "Update games" entry of the Meloni tab: downloads the games listed in the manifest of the
// meloni-games release onto the SD card.
//
// Manifest (built by the meloni-games CI):
//   {"format": 1, "files": [{"path": "roms/meloni/snake.mlg", "url": "snake.mlg",
//                            "size": 1234, "sha256": "...", "name": "Snake", "api": 1}, ...]}
// "url" is relative to the manifest (or absolute). Files that were installed by an earlier
// update and are gone from the manifest are deleted again. Other files are never touched.
#include <rg_system.h>
#include <cJSON.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meloni_update.h"

// Keep in sync with MEL_API_VERSION in meloni-games engine/meloni/meloni.h (at MELONI_COMMIT)
#define MELONI_API_VERSION 1
#define MELONI_DEFAULT_URL "https://github.com/RedPetroleum/meloni-games/releases/download/latest/manifest.json"
// Optional, to use another release: {"manifest_url": "https://..."}
#define MELONI_CONFIG_FILE RG_BASE_PATH_CONFIG "/meloni.json"
// Which files the updater installed, with their sha256
#define MELONI_STATE_FILE RG_BASE_PATH "/meloni/installed.json"
#define MANIFEST_MAX_SIZE (256 * 1024)

#if MBEDTLS_VERSION_NUMBER < 0x03000000
#define sha256_starts mbedtls_sha256_starts_ret
#define sha256_update mbedtls_sha256_update_ret
#define sha256_finish mbedtls_sha256_finish_ret
#else
#define sha256_starts mbedtls_sha256_starts
#define sha256_update mbedtls_sha256_update
#define sha256_finish mbedtls_sha256_finish
#endif

#ifdef RG_ENABLE_NETWORKING

static cJSON *read_json_file(const char *path)
{
    void *data = NULL;
    size_t size = 0;
    if (!rg_storage_read_file(path, &data, &size, 0))
        return NULL;
    cJSON *json = cJSON_ParseWithLength(data, size);
    free(data);
    return json;
}

static char *get_manifest_url(void)
{
    char *url = NULL;
    cJSON *config = read_json_file(MELONI_CONFIG_FILE);
    const char *value = cJSON_GetStringValue(cJSON_GetObjectItem(config, "manifest_url"));
    url = strdup(value && value[0] ? value : MELONI_DEFAULT_URL);
    cJSON_Delete(config);
    return url;
}

// Relative "url" entries are resolved against the manifest's folder
static char *resolve_url(const char *base, const char *url)
{
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0)
        return strdup(url);
    const char *slash = strrchr(base, '/');
    size_t base_len = slash ? (size_t)(slash - base + 1) : 0;
    char *out = malloc(base_len + strlen(url) + 1);
    if (out)
    {
        memcpy(out, base, base_len);
        strcpy(out + base_len, url);
    }
    return out;
}

// Only files below roms/ and romart/ may be written, and no ".."
static bool path_allowed(const char *path)
{
    return path && (strncmp(path, "roms/", 5) == 0 || strncmp(path, "romart/", 7) == 0) && !strstr(path, "..") &&
           !strchr(path, '\\');
}

static bool wait_for_network(void)
{
    rg_network_state_t state = rg_network_get_info().state;
    if (state == RG_NETWORK_CONNECTED)
        return true;
    if (state == RG_NETWORK_DISABLED)
    {
        rg_gui_alert("Wi-Fi", "Wi-Fi is not available.");
        return false;
    }

    if (state != RG_NETWORK_CONNECTING && !rg_network_wifi_start())
    {
        rg_gui_alert("Wi-Fi", "No Wi-Fi network configured.\n"
                              "Create /retro-go/config/wifi.json\n"
                              "(see the retro-go README).");
        return false;
    }

    int64_t deadline = rg_system_timer() + 20 * 1000000;
    while (rg_system_timer() < deadline)
    {
        if (rg_network_get_info().state == RG_NETWORK_CONNECTED)
            return true;
        rg_gui_draw_message("Connecting to Wi-Fi...\n(B: cancel)");
        if (rg_input_key_is_pressed(RG_KEY_B))
            break;
        rg_task_delay(250);
    }
    rg_gui_alert("Wi-Fi", "Could not connect.");
    return false;
}

static char *http_get_text(const char *url, int *status)
{
    rg_http_req_t *req = rg_network_http_open(url, NULL);
    *status = req ? req->status_code : 0;
    if (!req || req->status_code != 200)
    {
        rg_network_http_close(req);
        return NULL;
    }
    size_t capacity = 16 * 1024, length = 0;
    char *buffer = malloc(capacity + 1);
    int len;
    while (buffer && (len = rg_network_http_read(req, buffer + length, capacity - length)) > 0)
    {
        length += len;
        if (length == capacity)
        {
            char *bigger = capacity < MANIFEST_MAX_SIZE ? realloc(buffer, capacity * 2 + 1) : NULL;
            if (!bigger)
            {
                free(buffer);
                buffer = NULL;
                break;
            }
            buffer = bigger;
            capacity *= 2;
        }
    }
    rg_network_http_close(req);
    if (buffer)
        buffer[length] = 0;
    return buffer;
}

// Downloads url to RG_STORAGE_ROOT/path, checks size and sha256, replaces the old file only on success.
static bool download(const char *url, const char *path, const char *label, int index, int total,
                     int expected_size, const char *expected_sha, char *error, size_t error_len)
{
    char full_path[RG_PATH_MAX], temp_path[RG_PATH_MAX + 8];
    snprintf(full_path, sizeof(full_path), "%s/%s", RG_STORAGE_ROOT, path);
    snprintf(temp_path, sizeof(temp_path), "%s.part", full_path);
    rg_storage_mkdir(rg_dirname(full_path));

    rg_gui_draw_message("Updating %d/%d: %s\nConnecting...", index, total, label);
    rg_http_req_t *req = rg_network_http_open(url, NULL);
    if (!req || req->status_code != 200)
    {
        snprintf(error, error_len, "HTTP %d", req ? req->status_code : 0);
        rg_network_http_close(req);
        return false;
    }

    FILE *fp = fopen(temp_path, "wb");
    char *buffer = malloc(16 * 1024);
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    sha256_starts(&sha, 0);
    int received = 0, len = 0;
    bool ok = fp && buffer;
    while (ok && (len = rg_network_http_read(req, buffer, 16 * 1024)) > 0)
    {
        sha256_update(&sha, (const unsigned char *)buffer, len);
        ok = fwrite(buffer, 1, len, fp) == (size_t)len;
        received += len;
        rg_gui_draw_message("Updating %d/%d: %s\n%d / %d KB", index, total, label, received / 1024,
                            expected_size / 1024);
    }
    rg_network_http_close(req);
    free(buffer);
    if (fp)
        fclose(fp);

    unsigned char digest[32];
    char hex[65];
    sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    for (int i = 0; i < 32; i++)
        sprintf(hex + i * 2, "%02x", digest[i]);

    if (!ok || len < 0)
        snprintf(error, error_len, "download or write failed");
    else if (expected_size >= 0 && received != expected_size)
        snprintf(error, error_len, "size %d instead of %d", received, expected_size), ok = false;
    else if (expected_sha && strcasecmp(hex, expected_sha) != 0)
        snprintf(error, error_len, "checksum mismatch"), ok = false;

    if (!ok)
    {
        remove(temp_path);
        return false;
    }
    remove(full_path);
    if (rename(temp_path, full_path) != 0)
    {
        snprintf(error, error_len, "rename failed");
        remove(temp_path);
        return false;
    }
    return true;
}

static void save_state(cJSON *installed)
{
    char *text = cJSON_Print(installed);
    if (text)
    {
        rg_storage_mkdir(rg_dirname(MELONI_STATE_FILE));
        rg_storage_write_file(MELONI_STATE_FILE, text, strlen(text), 0);
        free(text);
    }
}

bool meloni_update_run(void)
{
    rg_network_state_t initial_state = rg_network_get_info().state;
    bool changed = false;
    char *manifest_url = get_manifest_url();
    char *text = NULL;
    cJSON *manifest = NULL, *installed = NULL;

    if (!wait_for_network())
        goto done;

    rg_gui_draw_message("Checking for updates...");
    int status = 0;
    if (!(text = http_get_text(manifest_url, &status)) || !(manifest = cJSON_Parse(text)))
    {
        char msg[300];
        if (status == 404)
            snprintf(msg, sizeof(msg), "No manifest found (HTTP 404).\nIs the repository public and\nis there a 'latest' release?");
        else
            snprintf(msg, sizeof(msg), "Could not load the manifest (HTTP %d).", status);
        rg_gui_alert("Update failed", msg);
        goto done;
    }

    installed = read_json_file(MELONI_STATE_FILE);
    if (!cJSON_IsObject(installed))
    {
        cJSON_Delete(installed);
        installed = cJSON_CreateObject();
    }

    // What to do: files whose checksum changed or that are missing, and files to remove
    cJSON *files = cJSON_GetObjectItem(manifest, "files");
    int count = cJSON_GetArraySize(files);
    int pending = 0, pending_kb = 0, too_new = 0, removals = 0;
    bool *todo = calloc(count + 1, sizeof(bool));
    for (int i = 0; i < count; i++)
    {
        cJSON *f = cJSON_GetArrayItem(files, i);
        const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(f, "path"));
        const char *sha = cJSON_GetStringValue(cJSON_GetObjectItem(f, "sha256"));
        cJSON *api = cJSON_GetObjectItem(f, "api");
        if (!path_allowed(path) || !cJSON_GetStringValue(cJSON_GetObjectItem(f, "url")))
            continue;
        if (cJSON_IsNumber(api) && api->valueint > MELONI_API_VERSION)
        {
            too_new++;
            continue;
        }
        char full_path[RG_PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", RG_STORAGE_ROOT, path);
        const char *have = cJSON_GetStringValue(cJSON_GetObjectItem(installed, path));
        if (!have || !sha || strcasecmp(have, sha) != 0 || !rg_storage_exists(full_path))
        {
            todo[i] = true;
            pending++;
            pending_kb += cJSON_GetNumberValue(cJSON_GetObjectItem(f, "size")) / 1024;
        }
    }
    cJSON *entry;
    cJSON_ArrayForEach(entry, installed)
    {
        bool listed = false;
        for (int i = 0; i < count && !listed; i++)
        {
            const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(files, i), "path"));
            listed = path && strcmp(path, entry->string) == 0;
        }
        removals += !listed;
    }

    if (pending == 0 && removals == 0)
    {
        rg_gui_alert("Meloni Games", too_new ? "Up to date. Some games need\na newer firmware." : "All games are up to date.");
        free(todo);
        goto done;
    }

    char question[128];
    snprintf(question, sizeof(question), "%d new or changed file(s), %d KB.\n%d to remove. Continue?", pending,
             pending_kb, removals);
    if (!rg_gui_confirm("Update games", question, true))
    {
        free(todo);
        goto done;
    }

    // Remove what is no longer in the release
    for (cJSON *e = installed->child; e;)
    {
        cJSON *next = e->next;
        bool listed = false;
        for (int i = 0; i < count && !listed; i++)
        {
            const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetArrayItem(files, i), "path"));
            listed = path && strcmp(path, e->string) == 0;
        }
        if (!listed && path_allowed(e->string))
        {
            char full_path[RG_PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", RG_STORAGE_ROOT, e->string);
            remove(full_path);
            cJSON_DeleteItemFromObject(installed, e->string);
            changed = true;
        }
        e = next;
    }
    save_state(installed);

    int done_count = 0, failed = 0, index = 0;
    char first_error[128] = "";
    for (int i = 0; i < count; i++)
    {
        if (!todo[i])
            continue;
        cJSON *f = cJSON_GetArrayItem(files, i);
        const char *path = cJSON_GetStringValue(cJSON_GetObjectItem(f, "path"));
        const char *sha = cJSON_GetStringValue(cJSON_GetObjectItem(f, "sha256"));
        cJSON *size = cJSON_GetObjectItem(f, "size");
        char *url = resolve_url(manifest_url, cJSON_GetStringValue(cJSON_GetObjectItem(f, "url")));
        char error[96] = "";
        if (url && download(url, path, rg_basename(path), ++index, pending, cJSON_IsNumber(size) ? size->valueint : -1,
                            sha, error, sizeof(error)))
        {
            cJSON_DeleteItemFromObject(installed, path);
            cJSON_AddStringToObject(installed, path, sha ? sha : "");
            save_state(installed);
            done_count++;
            changed = true;
        }
        else
        {
            RG_LOGE("Update of %s failed: %s", path, error);
            if (!failed++)
                snprintf(first_error, sizeof(first_error), "%s: %s", rg_basename(path), error);
        }
        free(url);
    }
    free(todo);

    char summary[256];
    snprintf(summary, sizeof(summary), "Updated: %d   Failed: %d%s%s%s", done_count, failed, failed ? "\n" : "",
             first_error, too_new ? "\nSome games need a newer firmware." : "");
    rg_gui_alert("Meloni Games", summary);

done:
    // Leave Wi-Fi as it was
    if (initial_state == RG_NETWORK_DISCONNECTED)
        rg_network_wifi_stop();
    cJSON_Delete(manifest);
    cJSON_Delete(installed);
    free(text);
    free(manifest_url);
    return changed;
}

#else

bool meloni_update_run(void)
{
    rg_gui_alert("Meloni Games", "This build has no network support.");
    return false;
}

#endif
