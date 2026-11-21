#ifndef FUNCTIONS7_H
#define FUNCTIONS7_H

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK,
    STATUS_ERR_NULL,
    STATUS_ERR_ALLOC,
    STATUS_ERR_OVERFLOW,
    STATUS_ERR_INPUT,
    STATUS_ERR_DATE,
    STATUS_ERR_NOT_FOUND,
    STATUS_ERR_EOF,
    STATUS_ERR_OPEN,
    STATUS_ERR_READ,
    STATUS_ERR_WRITE,
    STATUS_ERR_CLOSE
} status_t;

typedef struct {
    unsigned day, month, year;
} BirthDate;

typedef struct {
    const char* surname;
    const char* name;
    const char* patronymic;
    BirthDate birth;
    char sex;
    double income;
} Liver;

typedef struct Resident {
    Liver data;
    size_t id;
    struct Resident* next;
    char names[];
} Resident;

typedef struct Change {
    Resident* before;
    size_t after_id;
    struct Change* next;
} Change;

typedef struct {
    Resident* head;
    Change* history;
    size_t changes, last_id;
} Registry;

typedef struct {
    char* data;
    size_t capacity;
} Line;

static const char* status_message(status_t st){
    switch (st){
        case STATUS_OK:            return "ok";
        case STATUS_ERR_NULL:      return "null pointer";
        case STATUS_ERR_ALLOC:     return "memory allocation failed";
        case STATUS_ERR_OVERFLOW:  return "value is out of range";
        case STATUS_ERR_INPUT:     return "invalid input";
        case STATUS_ERR_DATE:      return "invalid birth date";
        case STATUS_ERR_NOT_FOUND: return "resident ID was not found";
        case STATUS_ERR_EOF:       return "end of input";
        case STATUS_ERR_OPEN:      return "cannot open file";
        case STATUS_ERR_READ:      return "cannot read input";
        case STATUS_ERR_WRITE:     return "cannot write output";
        case STATUS_ERR_CLOSE:     return "cannot close file";
    }
    return "unknown error";
}

// Reuse a line buffer initialized with {0}.
static status_t read_line(FILE* in, Line* line){
    if (in == NULL || line == NULL)
        return STATUS_ERR_NULL;
    if (line->data == NULL){
        line->data = malloc(64);
        if (line->data == NULL)
            return STATUS_ERR_ALLOC;
        line->capacity = 64;
    }

    size_t len = 0;
    status_t st = STATUS_OK;
    int c;
    while ((c = fgetc(in)) != EOF && c != '\n'){
        if (c == '\0')
            st = STATUS_ERR_INPUT;
        if (st != STATUS_OK)
            continue;
        if (len == line->capacity - 1){
            if (line->capacity == SIZE_MAX){
                st = STATUS_ERR_OVERFLOW;
                continue;
            }
            size_t cap = line->capacity <= SIZE_MAX / 2 ? line->capacity * 2 : SIZE_MAX;
            char* data = realloc(line->data, cap);
            if (data == NULL)
                return STATUS_ERR_ALLOC;
            line->data = data;
            line->capacity = cap;
        }
        line->data[len++] = (char)c;
    }
    if (c == EOF && ferror(in))
        return STATUS_ERR_READ;
    if (st != STATUS_OK)
        return st;
    if (c == EOF && len == 0)
        return STATUS_ERR_EOF;
    if (len > 0 && line->data[len - 1] == '\r')
        len--;
    line->data[len] = '\0';
    return STATUS_OK;
}

static char* trim(char* text){
    while (isspace((unsigned char)*text))
        text++;
    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        end--;
    *end = '\0';
    return text;
}

static status_t parse_id(const char* text, size_t* result){
    if (text == NULL || result == NULL)
        return STATUS_ERR_NULL;
    size_t id = 0;
    for (; *text != '\0'; text++){
        if (*text < '0' || *text > '9')
            return STATUS_ERR_INPUT;
        size_t digit = (size_t)(*text - '0');
        if (id > (SIZE_MAX - digit) / 10)
            return STATUS_ERR_OVERFLOW;
        id = id * 10 + digit;
    }
    if (id == 0)
        return STATUS_ERR_INPUT;
    *result = id;
    return STATUS_OK;
}

static status_t parse_date(const char* text, BirthDate* result){
    if (text == NULL || result == NULL)
        return STATUS_ERR_NULL;
    const char layout[] = "00.00.0000";
    if (strlen(text) != sizeof(layout) - 1)
        return STATUS_ERR_DATE;
    for (size_t i = 0; layout[i] != '\0'; i++)
        if (layout[i] == '.' ? text[i] != '.' : text[i] < '0' || text[i] > '9')
            return STATUS_ERR_DATE;

    BirthDate date;
    if (sscanf(text, "%2u.%2u.%4u", &date.day, &date.month, &date.year) != 3 ||
        date.day == 0 || date.month == 0 || date.month > 12 || date.year == 0)
        return STATUS_ERR_DATE;
    const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const unsigned leap = (unsigned)(date.year % 4 == 0 &&
                                     (date.year % 100 != 0 || date.year % 400 == 0));
    if (date.day > days[date.month - 1] + (date.month == 2 ? leap : 0u))
        return STATUS_ERR_DATE;
    *result = date;
    return STATUS_OK;
}

static unsigned long date_key(const BirthDate* birth){
    return birth->year * 10000UL + birth->month * 100UL + birth->day;
}

static status_t parse_income(const char* text, double* result){
    if (text == NULL || result == NULL)
        return STATUS_ERR_NULL;
    if (text[strspn(text, "0123456789.+-eE")] != '\0')
        return STATUS_ERR_INPUT;

    char* end;
    double value = strtod(text, &end);
    if (end == text || *end != '\0')
        return STATUS_ERR_INPUT;
    if (!isfinite(value) || (fpclassify(value) == FP_ZERO &&
        strcspn(text, "123456789") < strcspn(text, "eE")))
        return STATUS_ERR_OVERFLOW;
    if (fpclassify(value) != FP_ZERO && signbit(value))
        return STATUS_ERR_INPUT;
    *result = value;
    return STATUS_OK;
}

// Search wildcards use NULL names, a zero year or sex, and a negative income.
static status_t parse_liver(char* text, int search, Liver* result){
    if (text == NULL || result == NULL)
        return STATUS_ERR_NULL;
    const char* fields[6];
    for (size_t i = 0; i < 6; i++){
        char* separator = strchr(text, ';');
        if ((i < 5) != (separator != NULL))
            return STATUS_ERR_INPUT;
        if (separator != NULL)
            *separator = '\0';
        fields[i] = trim(text);
        if (search && strcmp(fields[i], "*") == 0)
            fields[i] = NULL;
        if (separator != NULL)
            text = separator + 1;
    }

    const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    for (size_t i = 0; i < 3; i++){
        const char* name = fields[i];
        if (name != NULL && ((i < 2 && *name == '\0') || name[strspn(name, letters)] != '\0'))
            return STATUS_ERR_INPUT;
    }
    Liver data = {.surname = fields[0], .name = fields[1], .patronymic = fields[2], .income = -1.0};
    if (fields[4] != NULL){
        if ((fields[4][0] != 'M' && fields[4][0] != 'W') || fields[4][1] != '\0')
            return STATUS_ERR_INPUT;
        data.sex = fields[4][0];
    }
    status_t st = fields[3] == NULL ? STATUS_OK : parse_date(fields[3], &data.birth);
    if (st == STATUS_OK && fields[5] != NULL)
        st = parse_income(fields[5], &data.income);
    if (st == STATUS_OK)
        *result = data;
    return st;
}

// The node owns its name strings in a single allocation.
static status_t make_resident(const Liver* data, size_t id, Resident** result){
    if (data == NULL || data->surname == NULL || data->name == NULL ||
        data->patronymic == NULL || result == NULL)
        return STATUS_ERR_NULL;
    if (id == 0)
        return STATUS_ERR_INPUT;

    const char* names[] = {data->surname, data->name, data->patronymic};
    size_t sizes[3];
    size_t total = sizeof(Resident);
    for (size_t i = 0; i < 3; i++){
        size_t len = strlen(names[i]);
        if (len >= SIZE_MAX - total)
            return STATUS_ERR_OVERFLOW;
        sizes[i] = len + 1;
        total += sizes[i];
    }
    Resident* node = malloc(total);
    if (node == NULL)
        return STATUS_ERR_ALLOC;
    node->data = *data;
    node->id = id;
    node->next = NULL;
    node->data.surname = memcpy(node->names, names[0], sizes[0]);
    node->data.name = memcpy(node->names + sizes[0], names[1], sizes[1]);
    node->data.patronymic = memcpy(node->names + sizes[0] + sizes[1], names[2], sizes[2]);
    *result = node;
    return STATUS_OK;
}

// Later births come first. IDs keep equal dates in a reproducible order.
static void insert_resident(Registry* registry, Resident* node){
    const unsigned long birth = date_key(&node->data.birth);
    Resident** link = &registry->head;
    while (*link != NULL){
        unsigned long current = date_key(&(*link)->data.birth);
        if (current < birth || (current == birth && (*link)->id > node->id))
            break;
        link = &(*link)->next;
    }
    node->next = *link;
    *link = node;
}

static Resident** resident_link(Registry* registry, size_t id){
    Resident** link = &registry->head;
    while (*link != NULL && (*link)->id != id)
        link = &(*link)->next;
    return link;
}

static status_t find_resident(const Resident* node, const Liver* query, double epsilon,
                              const Resident** result){
    if (result == NULL)
        return STATUS_ERR_NULL;
    if (!isfinite(epsilon) || epsilon < 0.0)
        return STATUS_ERR_INPUT;
    if (query != NULL){
        for (; node != NULL; node = node->next){
            const Liver* data = &node->data;
            if ((query->surname == NULL || strcmp(query->surname, data->surname) == 0) &&
                (query->name == NULL || strcmp(query->name, data->name) == 0) &&
                (query->patronymic == NULL || strcmp(query->patronymic, data->patronymic) == 0) &&
                (query->birth.year == 0 || date_key(&query->birth) == date_key(&data->birth)) &&
                (query->sex == '\0' || query->sex == data->sex) &&
                (query->income < 0.0 || fabs(query->income - data->income) <= epsilon))
                break;
        }
    }
    *result = node;
    return STATUS_OK;
}

// ID zero adds a record. NULL data deletes the record with the given ID.
static status_t change_resident(Registry* registry, size_t id, const Liver* data){
    if (registry == NULL)
        return STATUS_ERR_NULL;
    if (id == 0 && data == NULL)
        return STATUS_ERR_INPUT;
    if (registry->changes == SIZE_MAX || (id == 0 && registry->last_id == SIZE_MAX))
        return STATUS_ERR_OVERFLOW;
    Resident** link = id == 0 ? NULL : resident_link(registry, id);
    if (link != NULL && *link == NULL)
        return STATUS_ERR_NOT_FOUND;

    Resident* after = NULL;
    if (data != NULL){
        status_t st = make_resident(data, id == 0 ? registry->last_id + 1 : id, &after);
        if (st != STATUS_OK)
            return st;
    }
    Change* change = malloc(sizeof(*change));
    if (change == NULL){
        free(after);
        return STATUS_ERR_ALLOC;
    }

    // History owns the detached node until it is restored or discarded.
    Resident* before = link == NULL ? NULL : *link;
    if (before != NULL){
        *link = before->next;
        before->next = NULL;
    }
    if (after != NULL)
        insert_resident(registry, after);
    if (id == 0)
        registry->last_id++;
    *change = (Change){before, after == NULL ? 0 : after->id, registry->history};
    registry->history = change;
    registry->changes++;
    return STATUS_OK;
}

static void clear_history(Registry* registry){
    while (registry->history != NULL){
        Change* change = registry->history;
        registry->history = change->next;
        free(change->before);
        free(change);
    }
    registry->changes = 0;
}

static status_t undo_changes(Registry* registry, size_t* undone){
    if (registry == NULL || undone == NULL)
        return STATUS_ERR_NULL;
    const size_t count = registry->changes / 2;
    *undone = 0;
    if (count == 0)
        return STATUS_OK;

    for (; *undone < count; (*undone)++){
        Change* change = registry->history;
        if (change->after_id != 0){
            Resident** link = resident_link(registry, change->after_id);
            if (*link == NULL)
                return STATUS_ERR_NOT_FOUND;
            Resident* after = *link;
            *link = after->next;
            free(after);
        }
        if (change->before != NULL)
            insert_resident(registry, change->before);
        registry->history = change->next;
        registry->changes--;
        free(change);
    }
    clear_history(registry);
    return STATUS_OK;
}

static void destroy_registry(Registry* registry){
    if (registry == NULL)
        return;
    clear_history(registry);
    while (registry->head != NULL){
        Resident* node = registry->head;
        registry->head = node->next;
        free(node);
    }
    registry->last_id = 0;
}

#endif
