#include "gemini.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_in.h"
#include "audio_out.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "gemini";

#define API_HOST        "https://generativelanguage.googleapis.com"
#define TTS_RATE_HZ     24000       /* Gemini speech output is always 24 kHz */

#define REC_WAIT_MS     6000        /* how long to wait for you to start */
#define REC_MAX_MS      8000        /* hard cap on one request */
#define REC_HANG_MS     900         /* quiet needed to decide you finished */

/* Recording captures the silent wait too and trims afterwards, so the
 * buffer has to cover both. ~450 KB at 16 kHz — PSRAM. */
#define REC_SAMPLES     ((size_t)CONFIG_MOO_MIC_SAMPLE_RATE * \
                         (REC_WAIT_MS + REC_MAX_MS) / 1000)

#define STORY_MAX       2048        /* plenty for a 90-word Marathi story */

static gemini_stage_cb_t s_cb;
static void *s_cb_ctx;
static volatile bool s_busy;

static void stage(gemini_stage_t st, const char *detail)
{
    if (s_cb) {
        s_cb(st, detail, s_cb_ctx);
    }
}

/* ---- base64 ------------------------------------------------------------- */

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static inline size_t b64_len(size_t n)
{
    return ((n + 2) / 3) * 4;
}

/* Streaming encoder: keeps 0-2 bytes of carry between calls so the caller
 * can push arbitrary chunk sizes without emitting stray padding mid-body. */
typedef struct {
    uint8_t hold[3];
    int held;
} b64enc_t;

static size_t b64_encode_chunk(b64enc_t *e, const uint8_t *in, size_t n, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        e->hold[e->held++] = in[i];
        if (e->held == 3) {
            const uint32_t v = ((uint32_t)e->hold[0] << 16) |
                               ((uint32_t)e->hold[1] << 8) | e->hold[2];
            out[o++] = B64[(v >> 18) & 63];
            out[o++] = B64[(v >> 12) & 63];
            out[o++] = B64[(v >> 6) & 63];
            out[o++] = B64[v & 63];
            e->held = 0;
        }
    }
    return o;
}

static size_t b64_encode_final(b64enc_t *e, char *out)
{
    if (e->held == 0) {
        return 0;
    }
    const uint32_t v = ((uint32_t)e->hold[0] << 16) |
                       ((e->held > 1 ? (uint32_t)e->hold[1] : 0) << 8);
    out[0] = B64[(v >> 18) & 63];
    out[1] = B64[(v >> 12) & 63];
    out[2] = (e->held > 1) ? B64[(v >> 6) & 63] : '=';
    out[3] = '=';
    e->held = 0;
    return 4;
}

static int8_t b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;                       /* whitespace, padding, anything else */
}

/* ---- WAV ---------------------------------------------------------------- */

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = v >> 24;
}
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v & 0xff; p[1] = v >> 8;
}

/* Gemini accepts WAV/MP3/FLAC/OGG but not headerless PCM, so the mic buffer
 * gets a 44-byte hat. Free — no re-encoding. */
#define WAV_HDR_BYTES 44
static void wav_header(uint8_t *h, uint32_t rate, uint32_t data_bytes)
{
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);                /* PCM */
    put16(h + 22, 1);                /* mono */
    put32(h + 24, rate);
    put32(h + 28, rate * 2);         /* byte rate */
    put16(h + 32, 2);                /* block align */
    put16(h + 34, 16);               /* bits per sample */
    memcpy(h + 36, "data", 4);
    put32(h + 40, data_bytes);
}

/* ---- tiny JSON helpers -------------------------------------------------- */

/* Escape for a JSON string literal. UTF-8 passes through untouched, which
 * is what keeps Devanagari intact on the way back out to the TTS call. */
static size_t json_escape(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 7 < cap; p++) {
        switch (*p) {
        case '"':  out[o++] = '\\'; out[o++] = '"';  break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n';  break;
        case '\r': out[o++] = '\\'; out[o++] = 'r';  break;
        case '\t': out[o++] = '\\'; out[o++] = 't';  break;
        default:
            if (*p < 0x20) {
                o += snprintf(out + o, cap - o, "\\u%04x", *p);
            } else {
                out[o++] = (char)*p;
            }
        }
    }
    out[o] = '\0';
    return o;
}

static void utf8_put(char *out, size_t *o, size_t cap, uint32_t cp)
{
    if (cp < 0x80 && *o + 1 < cap) {
        out[(*o)++] = (char)cp;
    } else if (cp < 0x800 && *o + 2 < cap) {
        out[(*o)++] = (char)(0xC0 | (cp >> 6));
        out[(*o)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000 && *o + 3 < cap) {
        out[(*o)++] = (char)(0xE0 | (cp >> 12));
        out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*o)++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000 && *o + 4 < cap) {
        /* Decoded surrogate pairs land here. Devanagari is all BMP, but a
         * model that slips an emoji in would otherwise emit a broken byte. */
        out[(*o)++] = (char)(0xF0 | (cp >> 18));
        out[(*o)++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[(*o)++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[(*o)++] = (char)(0x80 | (cp & 0x3F));
    }
}

/* Pull the first "text": "..." out of a Gemini response and unescape it.
 * A full JSON parser would be nicer; this is 40 lines and the shape of the
 * reply is fixed. \uXXXX is handled because Google may escape Devanagari
 * rather than sending it raw, and the two look nothing alike downstream. */
static bool json_extract_text(const char *body, char *out, size_t cap)
{
    const char *p = strstr(body, "\"text\"");
    if (!p) {
        return false;
    }
    p += 6;
    while (*p && *p != '"') {
        p++;                          /* skip the colon and any spaces */
    }
    if (!*p) {
        return false;
    }
    p++;

    size_t o = 0;
    while (*p && o + 4 < cap) {
        if (*p == '"') {
            out[o] = '\0';
            return true;
        }
        if (*p != '\\') {
            out[o++] = *p++;
            continue;
        }
        p++;
        switch (*p) {
        case 'n':  out[o++] = '\n'; p++; break;
        case 'r':  out[o++] = '\r'; p++; break;
        case 't':  out[o++] = '\t'; p++; break;
        case 'b':  case 'f': p++; break;
        case 'u': {
            /* %x wants an unsigned int*, not an unsigned long* — writing a
             * long through a uint32_t* smashes the stack anywhere long is
             * 8 bytes, and only survives here by accident of ILP32. */
            unsigned int cp = 0;
            if (sscanf(p + 1, "%4x", &cp) == 1) {
                p += 5;
                /* Surrogate pair — rare in Devanagari, but a lone high
                 * surrogate renders as garbage. */
                if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                    unsigned int lo = 0;
                    if (sscanf(p + 2, "%4x", &lo) == 1 && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        p += 6;
                    }
                }
                utf8_put(out, &o, cap, (uint32_t)cp);
            } else {
                p++;
            }
            break;
        }
        default: out[o++] = *p++; break;
        }
    }
    out[o] = '\0';
    return o > 0;
}

/* ---- call 1: audio in, story text out ----------------------------------- */

static esp_err_t ask_for_story(const int16_t *pcm, size_t samples, char *story, size_t cap)
{
    char url[192];
    snprintf(url, sizeof(url),
             API_HOST "/v1beta/models/%s:generateContent?key=%s",
             CONFIG_MOO_GEMINI_MODEL, CONFIG_MOO_GEMINI_API_KEY);

    /* Heap for the same reason as speak_story(): locals here share the
     * stack with an mbedTLS handshake, and the cert-bundle verify is not
     * cheap. Told very explicitly to emit only the story — any preamble or
     * title would get read aloud by the TTS call verbatim. */
    const size_t prefix_cap = 1024;
    char *prefix = malloc(prefix_cap);
    ESP_RETURN_ON_FALSE(prefix, ESP_ERR_NO_MEM, TAG, "prefix buf");
    const int plen = snprintf(prefix, prefix_cap,
        "{\"system_instruction\":{\"parts\":[{\"text\":"
        "\"You are Moo, a gentle toy cow telling stories to a very young child "
        "(18 months old). Listen to the request in the audio and tell one short, "
        "warm, gentle story in %s. Use simple everyday words a toddler hears at "
        "home. Keep it under %d words. If the audio is unclear, tell a simple "
        "story about a friendly cow anyway. Output ONLY the story text itself: "
        "no title, no preamble, no explanation, no emoji, no romanisation, "
        "no markdown.\"}]},"
        "\"contents\":[{\"role\":\"user\",\"parts\":[{\"inline_data\":"
        "{\"mime_type\":\"audio/wav\",\"data\":\"",
        CONFIG_MOO_GEMINI_LANG, CONFIG_MOO_GEMINI_MAX_WORDS);

    static const char suffix[] = "\"}}]}],\"generationConfig\":{\"temperature\":1.0}}";

    const size_t pcm_bytes = samples * sizeof(int16_t);
    const size_t raw = WAV_HDR_BYTES + pcm_bytes;
    const size_t total = (size_t)plen + b64_len(raw) + strlen(suffix);

    const esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        free(prefix);
        ESP_LOGE(TAG, "http init");
        return ESP_FAIL;
    }

    esp_http_client_set_header(c, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(c, total);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open: %s", esp_err_to_name(err));
        free(prefix);
        esp_http_client_cleanup(c);
        return err;
    }

    if (esp_http_client_write(c, prefix, plen) < 0) {
        err = ESP_FAIL;
    }
    free(prefix);                        /* done with it the moment it is sent */

    /* Encode straight off the mic buffer. Building the whole ~340 KB JSON
     * body in RAM first would work but wastes most of a megabyte for no
     * reason; content-length is exactly computable up front instead. */
    if (err == ESP_OK) {
        b64enc_t enc = { 0 };
        char out[512];

        uint8_t hdr[WAV_HDR_BYTES];
        wav_header(hdr, CONFIG_MOO_MIC_SAMPLE_RATE, pcm_bytes);
        size_t n = b64_encode_chunk(&enc, hdr, sizeof(hdr), out);
        if (n && esp_http_client_write(c, out, n) < 0) {
            err = ESP_FAIL;
        }

        const uint8_t *p = (const uint8_t *)pcm;
        for (size_t done = 0; done < pcm_bytes && err == ESP_OK; ) {
            size_t take = pcm_bytes - done;
            if (take > 300) {
                take = 300;           /* 300 -> exactly 400 base64 chars */
            }
            n = b64_encode_chunk(&enc, p + done, take, out);
            if (n && esp_http_client_write(c, out, n) < 0) {
                err = ESP_FAIL;
            }
            done += take;
        }
        if (err == ESP_OK) {
            n = b64_encode_final(&enc, out);
            if (n && esp_http_client_write(c, out, n) < 0) {
                err = ESP_FAIL;
            }
        }
    }

    if (err == ESP_OK && esp_http_client_write(c, suffix, strlen(suffix)) < 0) {
        err = ESP_FAIL;
    }

    int status = 0;
    char *body = NULL;
    if (err == ESP_OK) {
        if (esp_http_client_fetch_headers(c) < 0) {
            err = ESP_FAIL;
        } else {
            status = esp_http_client_get_status_code(c);
            body = calloc(1, 8192);
            if (body) {
                int off = 0, r;
                while (off < 8191 &&
                       (r = esp_http_client_read(c, body + off, 8191 - off)) > 0) {
                    off += r;
                }
                body[off] = '\0';
            } else {
                err = ESP_ERR_NO_MEM;
            }
        }
    }

    esp_http_client_close(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK) {
        free(body);
        return err;
    }
    if (status != 200) {
        ESP_LOGE(TAG, "understand HTTP %d: %.300s", status, body ? body : "");
        free(body);
        return ESP_FAIL;
    }
    const bool ok = json_extract_text(body, story, cap);
    free(body);

    ESP_RETURN_ON_FALSE(ok, ESP_FAIL, TAG, "no text in reply");
    ESP_LOGI(TAG, "story (%u bytes): %.160s", (unsigned)strlen(story), story);
    return ESP_OK;
}

/* ---- call 2: story text in, speech straight to the speaker -------------- */

/* Walks the response looking for inlineData's "data", then base64-decodes
 * into the speaker as bytes arrive. The story is megabytes of PCM; holding
 * it would mean a multi-megabyte allocation and silence until the whole
 * download finished. audio_out_pcm_write() blocking on a full buffer is
 * also the only flow control this needs. */
typedef enum { SC_FIND_KEY, SC_FIND_QUOTE, SC_DECODE, SC_DONE } scan_state_t;

typedef struct {
    scan_state_t state;
    int key_match;            /* how much of "data" we have matched */
    uint32_t acc;
    int acc_n;
    size_t pcm_bytes;
    esp_err_t err;
} tts_scan_t;

static void tts_feed(tts_scan_t *s, const char *buf, int len)
{
    static const char KEY[] = "\"data\"";
    uint8_t out[192];
    int o = 0;

    for (int i = 0; i < len && s->state != SC_DONE; i++) {
        const char ch = buf[i];

        switch (s->state) {
        case SC_FIND_KEY:
            s->key_match = (ch == KEY[s->key_match]) ? s->key_match + 1
                                                     : (ch == KEY[0] ? 1 : 0);
            if (s->key_match == (int)sizeof(KEY) - 1) {
                s->state = SC_FIND_QUOTE;
            }
            break;

        case SC_FIND_QUOTE:
            if (ch == '"') {
                s->state = SC_DECODE;
                s->acc = 0;
                s->acc_n = 0;
            }
            break;

        case SC_DECODE: {
            if (ch == '"') {
                /* Flush the final, unpadded group. Base64 ending in '=' or
                 * '==' encodes 2 or 1 trailing bytes, which never reach a
                 * full quartet — without this the last sample of every
                 * story is silently dropped. */
                if (s->acc_n == 3) {
                    out[o++] = (uint8_t)(s->acc >> 10);
                    out[o++] = (uint8_t)(s->acc >> 2);
                } else if (s->acc_n == 2) {
                    out[o++] = (uint8_t)(s->acc >> 4);
                }
                s->acc = 0;
                s->acc_n = 0;
                s->state = SC_DONE;
                break;
            }
            const int8_t v = b64_val(ch);
            if (v < 0) {
                break;                /* newline inside the payload, or '=' */
            }
            s->acc = (s->acc << 6) | (uint32_t)v;
            if (++s->acc_n == 4) {
                out[o++] = (uint8_t)(s->acc >> 16);
                out[o++] = (uint8_t)(s->acc >> 8);
                out[o++] = (uint8_t)s->acc;
                s->acc = 0;
                s->acc_n = 0;
                if (o > (int)sizeof(out) - 4) {
                    if (s->err == ESP_OK) {
                        s->err = audio_out_pcm_write(out, o);
                    }
                    s->pcm_bytes += o;
                    o = 0;
                }
            }
            break;
        }
        default:
            break;
        }
    }

    if (o) {
        if (s->err == ESP_OK) {
            s->err = audio_out_pcm_write(out, o);
        }
        s->pcm_bytes += o;
    }
}

static esp_err_t speak_story(const char *story)
{
    /* Heap, not stack. This used to be a STORY_MAX*2 array here, and 4 KB
     * of locals plus the mbedTLS handshake for the second connection blew
     * the task stack right after the story came back. Worst case per input
     * byte is 6 chars (\u00XX); UTF-8 passes through 1:1. */
    const size_t esc_cap = strlen(story) * 6 + 16;
    char *escaped = malloc(esc_cap);
    ESP_RETURN_ON_FALSE(escaped, ESP_ERR_NO_MEM, TAG, "tts escape buf");
    json_escape(story, escaped, esc_cap);

    char url[192];
    snprintf(url, sizeof(url),
             API_HOST "/v1beta/models/%s:generateContent?key=%s",
             CONFIG_MOO_GEMINI_TTS_MODEL, CONFIG_MOO_GEMINI_API_KEY);

    const size_t cap = strlen(escaped) + 512;
    char *body = malloc(cap);
    if (!body) {
        free(escaped);
        ESP_LOGE(TAG, "no mem for tts body");
        return ESP_ERR_NO_MEM;
    }

    const int blen = snprintf(body, cap,
        "{\"contents\":[{\"parts\":[{\"text\":"
        "\"Read this warmly and slowly, like a bedtime story for a small child: %s\"}]}],"
        "\"generationConfig\":{\"responseModalities\":[\"AUDIO\"],"
        "\"speechConfig\":{\"voiceConfig\":{\"prebuiltVoiceConfig\":"
        "{\"voiceName\":\"%s\"}}}}}",
        escaped, CONFIG_MOO_GEMINI_VOICE);
    free(escaped);

    const esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) {
        free(body);
        return ESP_FAIL;
    }
    esp_http_client_set_header(c, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_open(c, blen);
    if (err == ESP_OK && esp_http_client_write(c, body, blen) < 0) {
        err = ESP_FAIL;
    }
    free(body);

    int status = 0;
    if (err == ESP_OK) {
        if (esp_http_client_fetch_headers(c) < 0) {
            err = ESP_FAIL;
        } else {
            status = esp_http_client_get_status_code(c);
        }
    }

    if (err == ESP_OK && status == 200) {
        err = audio_out_pcm_begin(TTS_RATE_HZ);
        if (err == ESP_OK) {
            tts_scan_t scan = { .state = SC_FIND_KEY, .err = ESP_OK };
            char chunk[1024];
            int r;
            while ((r = esp_http_client_read(c, chunk, sizeof(chunk))) > 0) {
                tts_feed(&scan, chunk, r);
            }
            /* end() always runs — skipping it would leave audio locked. */
            const esp_err_t e = audio_out_pcm_end();
            err = (scan.err != ESP_OK) ? scan.err : e;
            ESP_LOGI(TAG, "spoke %u bytes of PCM (%.1f s)",
                     (unsigned)scan.pcm_bytes,
                     (float)scan.pcm_bytes / (2.0f * TTS_RATE_HZ));
            if (scan.pcm_bytes == 0) {
                ESP_LOGE(TAG, "no audio in the TTS reply");
                err = ESP_FAIL;
            }
        }
    } else if (err == ESP_OK) {
        char peek[300] = { 0 };
        esp_http_client_read(c, peek, sizeof(peek) - 1);
        ESP_LOGE(TAG, "tts HTTP %d: %s", status, peek);
        err = ESP_FAIL;
    }

    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return err;
}

/* ---- task --------------------------------------------------------------- */

static void story_task(void *arg)
{
    (void)arg;
    int16_t *rec = heap_caps_malloc(REC_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    char *story = calloc(1, STORY_MAX);
    esp_err_t err = (rec && story) ? ESP_OK : ESP_ERR_NO_MEM;

    if (err == ESP_OK) {
        stage(GEMINI_STAGE_LISTENING, NULL);
        size_t n = 0;
        err = audio_in_record(rec, REC_SAMPLES, &n, REC_WAIT_MS, REC_MAX_MS, REC_HANG_MS);

        if (err == ESP_ERR_TIMEOUT) {
            stage(GEMINI_STAGE_FAILED, "didn't hear you");
        } else if (err == ESP_OK) {
            stage(GEMINI_STAGE_THINKING, NULL);
            err = ask_for_story(rec, n, story, STORY_MAX);
            if (err == ESP_OK) {
                stage(GEMINI_STAGE_SPEAKING, story);
                err = speak_story(story);
            }
        }
    }

    free(rec);
    free(story);

    if (err == ESP_OK) {
        stage(GEMINI_STAGE_DONE, NULL);
    } else if (err != ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "story failed: %s", esp_err_to_name(err));
        stage(GEMINI_STAGE_FAILED, esp_err_to_name(err));
    }

    /* Prints the smallest the stack ever got. This task overflowed once
     * already — if this number approaches zero, raise the stack rather than
     * waiting for the next panic. */
    ESP_LOGI(TAG, "story task stack headroom: %u bytes",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

    s_busy = false;
    vTaskDelete(NULL);
}

/* ---- api ---------------------------------------------------------------- */

bool gemini_configured(void)
{
    return CONFIG_MOO_GEMINI_API_KEY[0] != '\0';
}

bool gemini_busy(void)
{
    return s_busy;
}

esp_err_t gemini_init(gemini_stage_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    if (!gemini_configured()) {
        ESP_LOGW(TAG, "no API key — stories disabled "
                      "(menuconfig -> MooBot Gemini)");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "ready: %s -> %s (%s, voice %s)", CONFIG_MOO_GEMINI_MODEL,
             CONFIG_MOO_GEMINI_TTS_MODEL, CONFIG_MOO_GEMINI_LANG,
             CONFIG_MOO_GEMINI_VOICE);
    return ESP_OK;
}

esp_err_t gemini_ask_story(void)
{
    ESP_RETURN_ON_FALSE(gemini_configured(), ESP_ERR_INVALID_STATE, TAG, "no API key");
    ESP_RETURN_ON_FALSE(!s_busy, ESP_ERR_INVALID_STATE, TAG, "already running");

    s_busy = true;
    /* Two sequential TLS handshakes with the full cert bundle, plus the
     * base64 chunk buffer and LVGL work done from the stage callback. 8192
     * overflowed the moment the story came back; the big JSON buffers are
     * on the heap now, and this is the margin on top of that. */
    if (xTaskCreate(story_task, "story", 16384, NULL, 4, NULL) != pdPASS) {
        s_busy = false;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
