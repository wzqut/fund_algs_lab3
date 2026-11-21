#ifndef FUNCTIONS4_H
#define FUNCTIONS4_H

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define INDEX_LEN 6
#define MAIL_ID_LEN 14
#define DATETIME_LEN 19
#define STRING_LIMIT ((uintmax_t)INT_MAX < SIZE_MAX ? (size_t)INT_MAX : SIZE_MAX - 1)

typedef enum {
    STATUS_OK,
    STATUS_ERR_NULL,
    STATUS_ERR_ALLOC,
    STATUS_ERR_OVERFLOW,
    STATUS_ERR_INPUT,
    STATUS_ERR_DATE,
    STATUS_ERR_DUPLICATE,
    STATUS_ERR_NOT_FOUND,
    STATUS_ERR_TIME,
    STATUS_ERR_EOF,
    STATUS_ERR_READ,
    STATUS_ERR_WRITE
} status_t;

// Initialize with {0} before first use. Each String owns its buffer.
typedef struct {
    char* data;
    int length;
} String;

typedef struct {
    String city;
    String street;
    unsigned int house;
    String building;
    unsigned int apartment;
    String index;
} Address;

typedef struct {
    Address recipient;
    double weight;
    String id;
    String created;
    String deadline;
    int delivered;
} Mail;

typedef struct {
    const Address* address;
    Mail* items;
    size_t count;
    size_t capacity;
} Post;

typedef enum {
    FILTER_DELIVERED,
    FILTER_EXPIRED
} filter_t;

const char* status_message(status_t st){
    switch (st){
        case STATUS_OK:                return "ok";
        case STATUS_ERR_NULL:          return "null pointer";
        case STATUS_ERR_ALLOC:         return "memory allocation failed";
        case STATUS_ERR_OVERFLOW:      return "value is out of range";
        case STATUS_ERR_INPUT:         return "invalid input";
        case STATUS_ERR_DATE:          return "invalid date or delivery deadline";
        case STATUS_ERR_DUPLICATE:     return "mail id already exists";
        case STATUS_ERR_NOT_FOUND:     return "mail id was not found";
        case STATUS_ERR_TIME:          return "cannot get local time";
        case STATUS_ERR_EOF:           return "end of input";
        case STATUS_ERR_READ:          return "cannot read input";
        case STATUS_ERR_WRITE:         return "cannot write output";
    }
    return "unknown error";
}

static const char* string_data(const String* s){
    return s->data != NULL ? s->data : "";
}

void string_destroy(String* s){
    if (s == NULL)
        return;
    free(s->data);
    *s = (String){0};
}

// Copy both parts before replacing the buffer, which either part may refer to.
static status_t string_join(String* result, const char* first, size_t first_len,
                            const char* second, size_t second_len){
    if (first_len > STRING_LIMIT || second_len > STRING_LIMIT - first_len)
        return STATUS_ERR_OVERFLOW;
    size_t len = first_len + second_len;
    char* data = malloc(len + 1);
    if (data == NULL)
        return STATUS_ERR_ALLOC;
    memcpy(data, first, first_len);
    memcpy(data + first_len, second, second_len);
    data[len] = '\0';
    free(result->data);
    *result = (String){data, (int)len};
    return STATUS_OK;
}

status_t string_create(const char* text, String* result){
    if (text == NULL || result == NULL)
        return STATUS_ERR_NULL;
    return string_join(result, text, strlen(text), "", 0);
}

// Order by length, then lexicographically.
status_t string_compare(const String* a, const String* b, int* result){
    if (a == NULL || b == NULL || result == NULL)
        return STATUS_ERR_NULL;
    int cmp = (a->length > b->length) - (a->length < b->length);
    if (cmp == 0)
        cmp = strcmp(string_data(a), string_data(b));
    *result = (cmp > 0) - (cmp < 0);
    return STATUS_OK;
}

status_t string_equal(const String* a, const String* b, int* result){
    status_t st = string_compare(a, b, result);
    if (st == STATUS_OK)
        *result = *result == 0;
    return st;
}

status_t string_copy(const String* source, String* dest){
    if (source == NULL || dest == NULL)
        return STATUS_ERR_NULL;
    if (source == dest)
        return STATUS_OK;
    return string_join(dest, string_data(source), (size_t)source->length, "", 0);
}

status_t string_clone(const String* source, String** result){
    if (source == NULL || result == NULL)
        return STATUS_ERR_NULL;
    *result = NULL;
    String* copy = malloc(sizeof(*copy));
    if (copy == NULL)
        return STATUS_ERR_ALLOC;
    *copy = (String){0};
    status_t st = string_copy(source, copy);
    if (st != STATUS_OK){
        free(copy);
        return st;
    }
    *result = copy;
    return STATUS_OK;
}

status_t string_concat(String* dest, const String* suffix){
    if (dest == NULL || suffix == NULL)
        return STATUS_ERR_NULL;
    return string_join(dest, string_data(dest), (size_t)dest->length,
                       string_data(suffix), (size_t)suffix->length);
}

static int is_blank(const String* s){
    return strspn(string_data(s), " \t\n\v\f\r") == (size_t)s->length;
}

static int digits_only(const String* s, int len){
    return s->length == len && strspn(string_data(s), "0123456789") == (size_t)len;
}

status_t parse_positive(const String* s, unsigned int* result){
    if (s == NULL || result == NULL)
        return STATUS_ERR_NULL;
    if (s->length == 0 || !digits_only(s, s->length))
        return STATUS_ERR_INPUT;

    unsigned int value = 0;
    for (int i = 0; i < s->length; i++){
        unsigned int digit = (unsigned int)(s->data[i] - '0');
        if (value > (UINT_MAX - digit) / 10u)
            return STATUS_ERR_OVERFLOW;
        value = value * 10u + digit;
    }
    if (value == 0)
        return STATUS_ERR_INPUT;
    *result = value;
    return STATUS_OK;
}

status_t parse_weight(const String* s, double* result){
    if (s == NULL || result == NULL)
        return STATUS_ERR_NULL;
    const char* text = string_data(s);
    if (*text == '\0' || strspn(text, "0123456789.eE+-") != (size_t)s->length)
        return STATUS_ERR_INPUT;

    char* end;
    double value = strtod(text, &end);
    if (end == text || *end != '\0')
        return STATUS_ERR_INPUT;
    if (!isfinite(value))
        return STATUS_ERR_OVERFLOW;
    int zero = fpclassify(value) == FP_ZERO;
    if (zero && strcspn(text, "123456789") < strcspn(text, "eE"))
        return STATUS_ERR_OVERFLOW;
    if (!zero && signbit(value))
        return STATUS_ERR_INPUT;
    *result = value;
    return STATUS_OK;
}

static int valid_datetime(const String* date){
    if (date->length != DATETIME_LEN)
        return 0;
    const char* s = string_data(date);
    const char* layout = "00:00:0000 00:00:00";
    for (int i = 0; i < DATETIME_LEN; i++)
        if (layout[i] == '0' ? s[i] < '0' || s[i] > '9' : s[i] != layout[i])
            return 0;
    int day, month, year, hour, minute, second;
    if (sscanf(s, "%2d:%2d:%4d %2d:%2d:%2d", &day, &month, &year, &hour, &minute, &second) != 6 ||
        year == 0 || month < 1 || month > 12 || day < 1)
        return 0;
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return day <= days[month - 1] + (month == 2 && leap) &&
           hour < 24 && minute < 60 && second < 60;
}

// Valid dates have fixed-width fields. Compare year, month, day, then time.
static int compare_datetime(const char* a, const char* b){
    int cmp = strncmp(a + 6, b + 6, 4);
    if (cmp == 0)
        cmp = strncmp(a + 3, b + 3, 2);
    if (cmp == 0)
        cmp = strncmp(a, b, 2);
    return cmp != 0 ? cmp : strcmp(a + 11, b + 11);
}

status_t address_validate(const Address* a){
    if (a == NULL)
        return STATUS_ERR_NULL;
    if (is_blank(&a->city) || is_blank(&a->street) || a->house == 0 ||
        a->apartment == 0 || !digits_only(&a->index, INDEX_LEN))
        return STATUS_ERR_INPUT;
    return STATUS_OK;
}

void address_destroy(Address* a){
    if (a == NULL)
        return;
    string_destroy(&a->city);
    string_destroy(&a->street);
    string_destroy(&a->building);
    string_destroy(&a->index);
}

void mail_destroy(Mail* mail){
    if (mail == NULL)
        return;
    address_destroy(&mail->recipient);
    string_destroy(&mail->id);
    string_destroy(&mail->created);
    string_destroy(&mail->deadline);
    *mail = (Mail){0};
}

void post_destroy(Post* post){
    if (post == NULL)
        return;
    for (size_t i = 0; i < post->count; i++)
        mail_destroy(&post->items[i]);
    free(post->items);
    *post = (Post){0};
}

status_t post_find(const Post* post, const String* id, size_t* result){
    if (post == NULL || id == NULL || result == NULL)
        return STATUS_ERR_NULL;
    if (!digits_only(id, MAIL_ID_LEN))
        return STATUS_ERR_INPUT;

    for (size_t i = 0; i < post->count; i++)
        if (strcmp(post->items[i].id.data, id->data) == 0){
            *result = i;
            return STATUS_OK;
        }
    return STATUS_ERR_NOT_FOUND;
}

static int compare_mail(const Mail* a, const Mail* b){
    int cmp = strcmp(a->recipient.index.data, b->recipient.index.data);
    return cmp != 0 ? cmp : strcmp(a->id.data, b->id.data);
}

// Move the mail into the sorted array on success.
status_t post_add(Post* post, Mail* mail){
    if (post == NULL || mail == NULL)
        return STATUS_ERR_NULL;
    status_t st = address_validate(&mail->recipient);
    if (st != STATUS_OK)
        return st;
    if (!isfinite(mail->weight) || (fpclassify(mail->weight) != FP_ZERO && signbit(mail->weight)))
        return STATUS_ERR_INPUT;
    if (!valid_datetime(&mail->created) || !valid_datetime(&mail->deadline) ||
        compare_datetime(mail->created.data, mail->deadline.data) > 0)
        return STATUS_ERR_DATE;

    size_t pos;
    st = post_find(post, &mail->id, &pos);
    if (st != STATUS_ERR_NOT_FOUND)
        return st == STATUS_OK ? STATUS_ERR_DUPLICATE : st;

    if (post->count == post->capacity){
        size_t limit = SIZE_MAX / sizeof(Mail);
        if (post->capacity == limit)
            return STATUS_ERR_OVERFLOW;
        size_t cap = post->capacity == 0 ? 1 : post->capacity;
        cap = cap <= limit / 2 ? cap * 2 : limit;
        Mail* items = realloc(post->items, cap * sizeof(*items));
        if (items == NULL)
            return STATUS_ERR_ALLOC;
        post->items = items;
        post->capacity = cap;
    }
    pos = 0;
    while (pos < post->count && compare_mail(&post->items[pos], mail) < 0)
        pos++;
    if (pos < post->count)
        memmove(&post->items[pos + 1], &post->items[pos], (post->count - pos) * sizeof(Mail));
    post->items[pos] = *mail;
    post->items[pos].delivered = 0;
    post->count++;
    *mail = (Mail){0};
    return STATUS_OK;
}

status_t post_remove(Post* post, const String* id){
    size_t pos;
    status_t st = post_find(post, id, &pos);
    if (st != STATUS_OK)
        return st;
    mail_destroy(&post->items[pos]);
    post->count--;
    if (pos < post->count)
        memmove(&post->items[pos], &post->items[pos + 1], (post->count - pos) * sizeof(Mail));
    post->items[post->count] = (Mail){0};
    return STATUS_OK;
}

status_t post_deliver(Post* post, const String* id){
    size_t pos;
    status_t st = post_find(post, id, &pos);
    if (st == STATUS_OK)
        post->items[pos].delivered = 1;
    return st;
}

static int compare_created(const void* a, const void* b){
    const Mail* x = *(const Mail* const*)a;
    const Mail* y = *(const Mail* const*)b;
    int cmp = compare_datetime(x->created.data, y->created.data);
    return cmp != 0 ? cmp : compare_mail(x, y);
}

// Return a sorted array of borrowed pointers, valid until the post changes.
status_t post_filter(const Post* post, filter_t filter, const Mail*** result, size_t* count){
    if (post == NULL || result == NULL || count == NULL)
        return STATUS_ERR_NULL;
    *result = NULL;
    *count = 0;
    if (filter != FILTER_DELIVERED && filter != FILTER_EXPIRED)
        return STATUS_ERR_INPUT;
    if (post->count == 0)
        return STATUS_OK;

    char now[DATETIME_LEN + 1];
    if (filter == FILTER_EXPIRED){
        time_t current = time(NULL);
        if (current == (time_t)-1)
            return STATUS_ERR_TIME;
        const struct tm* local = localtime(&current);
        if (local == NULL || strftime(now, sizeof(now), "%d:%m:%Y %H:%M:%S", local) != DATETIME_LEN)
            return STATUS_ERR_TIME;
    }

    if (post->count > SIZE_MAX / sizeof(const Mail*))
        return STATUS_ERR_OVERFLOW;
    const Mail** matches = malloc(post->count * sizeof(*matches));
    if (matches == NULL)
        return STATUS_ERR_ALLOC;
    for (size_t i = 0; i < post->count; i++){
        const Mail* mail = &post->items[i];
        int match = filter == FILTER_DELIVERED ? mail->delivered :
                    !mail->delivered && compare_datetime(mail->deadline.data, now) <= 0;
        if (match)
            matches[(*count)++] = mail;
    }
    qsort(matches, *count, sizeof(const Mail*), compare_created);
    *result = matches;
    return STATUS_OK;
}

status_t read_line(FILE* in, String* result){
    if (in == NULL || result == NULL)
        return STATUS_ERR_NULL;
    const size_t limit = STRING_LIMIT + 1;
    size_t cap = 64, len = 0;
    char* buf = malloc(cap);
    if (buf == NULL)
        return STATUS_ERR_ALLOC;

    status_t st = STATUS_OK;
    int c;
    while ((c = fgetc(in)) != EOF && c != '\n'){
        if (c == '\0')
            st = STATUS_ERR_INPUT;
        if (st != STATUS_OK)
            continue;
        if (len == STRING_LIMIT){
            st = STATUS_ERR_OVERFLOW;
            continue;
        }
        if (len + 1 == cap){
            cap = cap <= limit / 2 ? cap * 2 : limit;
            char* tmp = realloc(buf, cap);
            if (tmp == NULL){
                st = STATUS_ERR_ALLOC;
                break;
            }
            buf = tmp;
        }
        buf[len++] = (char)c;
    }
    if (c == EOF && ferror(in))
        st = STATUS_ERR_READ;
    else if (c == EOF && len == 0 && st == STATUS_OK)
        st = STATUS_ERR_EOF;
    if (st != STATUS_OK){
        free(buf);
        return st;
    }
    if (len > 0 && buf[len - 1] == '\r')
        len--;
    buf[len] = '\0';
    string_destroy(result);
    *result = (String){buf, (int)len};
    return STATUS_OK;
}

status_t write_mail(FILE* out, const Mail* mail){
    if (out == NULL || mail == NULL)
        return STATUS_ERR_NULL;
    const Address* a = &mail->recipient;
    int written = fprintf(out, "Mail id: %s\n"
                               "Recipient: %s, %s, house %u, building %s, apartment %u, index %s\n"
                               "Weight: %.15g\nCreated: %s\nDelivery deadline: %s\nStatus: %s\n\n",
                          string_data(&mail->id), string_data(&a->city), string_data(&a->street),
                          a->house, a->building.length > 0 ? a->building.data : "-", a->apartment,
                          string_data(&a->index), mail->weight, string_data(&mail->created),
                          string_data(&mail->deadline), mail->delivered ? "delivered" : "pending");
    return written < 0 ? STATUS_ERR_WRITE : STATUS_OK;
}

#endif
