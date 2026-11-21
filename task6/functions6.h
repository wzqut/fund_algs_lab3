#ifndef FUNCTIONS6_H
#define FUNCTIONS6_H

#include <ctype.h>
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
    STATUS_ERR_ROUTE,
    STATUS_ERR_EMPTY,
    STATUS_ERR_EOF,
    STATUS_ERR_OPEN,
    STATUS_ERR_READ,
    STATUS_ERR_CLOSE,
    STATUS_ERR_WRITE
} status_t;

typedef enum { START, INTERMEDIATE, END } marker_t;
typedef enum {
    ROUTE_COUNT, TOTAL_DISTANCE, ROUTE_DISTANCE, STOP_DURATION, TOTAL_STOP_DURATION
} metric_t;

typedef struct {
    double x, y;
} Point;

typedef struct Stop {
    Point point;
    uintmax_t arrival, departure;
    marker_t marker;
    struct Stop* next;
} Stop;

typedef struct {
    uintmax_t routes, shortest_stop, longest_stop, total_stop;
    double shortest_route, longest_route, total_distance;
} Stats;

typedef struct Vehicle {
    Stop* stops;
    Stats stats;
    struct Vehicle* next;
    char id[];
} Vehicle;

typedef struct {
    char* data;
    size_t capacity;
} Line;

typedef struct {
    uintmax_t whole;
    double distance;
} Measure;

static const char* status_message(status_t st){
    switch (st){
        case STATUS_OK:            return "ok";
        case STATUS_ERR_NULL:      return "null pointer";
        case STATUS_ERR_ALLOC:     return "memory allocation failed";
        case STATUS_ERR_OVERFLOW:  return "value is out of range";
        case STATUS_ERR_INPUT:     return "invalid input";
        case STATUS_ERR_DATE:      return "invalid date or departure time";
        case STATUS_ERR_ROUTE:     return "invalid route markers or stop order";
        case STATUS_ERR_EMPTY:     return "no vehicle records";
        case STATUS_ERR_EOF:       return "end of input";
        case STATUS_ERR_OPEN:      return "cannot open file";
        case STATUS_ERR_READ:      return "cannot read input";
        case STATUS_ERR_CLOSE:     return "cannot close file";
        case STATUS_ERR_WRITE:     return "cannot write output";
    }
    return "unknown error";
}

// Reuse the line buffer, initialized with {0}, across reads.
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
        if (len + 1 == line->capacity){
            size_t cap = line->capacity;
            if (cap == SIZE_MAX)
                return STATUS_ERR_OVERFLOW;
            cap = cap <= SIZE_MAX / 2 ? cap * 2 : SIZE_MAX;
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

static status_t parse_coordinate(const char* text, double* result){
    if (strspn(text, "0123456789.+-eE") != strlen(text))
        return STATUS_ERR_INPUT;
    char* end;
    double value = strtod(text, &end);
    if (end == text || *end != '\0')
        return STATUS_ERR_INPUT;
    if (!isfinite(value) || (fpclassify(value) == FP_ZERO &&
        strcspn(text, "123456789") < strcspn(text, "eE")))
        return STATUS_ERR_OVERFLOW;
    *result = value;
    return STATUS_OK;
}

static status_t parse_point(char* line, Point* point){
    if (line == NULL || point == NULL)
        return STATUS_ERR_NULL;
    char* x = trim(line);
    char* y = x + strcspn(x, " \t\r\v\f");
    if (*y == '\0')
        return STATUS_ERR_INPUT;
    *y++ = '\0';
    status_t st = parse_coordinate(x, &point->x);
    return st == STATUS_OK ? parse_coordinate(trim(y), &point->y) : st;
}

static status_t parse_datetime(const char* text, uintmax_t* result){
    const char layout[] = "00.00.0000 00:00:00";
    if (strlen(text) != sizeof(layout) - 1)
        return STATUS_ERR_DATE;
    for (size_t i = 0; layout[i] != '\0'; i++)
        if (layout[i] == '0' ? text[i] < '0' || text[i] > '9' : text[i] != layout[i])
            return STATUS_ERR_DATE;

    unsigned day, month, year, hour, minute, second;
    if (sscanf(text, "%2u.%2u.%4u %2u:%2u:%2u", &day, &month, &year,
               &hour, &minute, &second) != 6 ||
        year == 0 || month < 1 || month > 12 || day < 1 ||
        hour > 23 || minute > 59 || second > 59)
        return STATUS_ERR_DATE;

    const unsigned days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const unsigned leap = (unsigned)(year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (day > days_in_month[month - 1] + (month == 2 ? leap : 0))
        return STATUS_ERR_DATE;

    // Gregorian seconds since 01.01.0001, independent of time zones and time_t.
    const uintmax_t years = year - 1;
    uintmax_t days = years * 365 + years / 4 - years / 100 + years / 400 + day - 1;
    for (unsigned i = 1; i < month; i++)
        days += days_in_month[i - 1] + (i == 2 ? leap : 0);
    *result = ((days * 24 + hour) * 60 + minute) * 60 + second;
    return STATUS_OK;
}

static status_t parse_record(char* line, const char** id, Stop* stop){
    if (line == NULL || id == NULL || stop == NULL)
        return STATUS_ERR_NULL;

    char* fields[4];
    for (size_t i = 0; i < 3; i++){
        char* separator = strchr(line, ';');
        if (separator == NULL)
            return STATUS_ERR_INPUT;
        *separator = '\0';
        fields[i] = trim(line);
        line = separator + 1;
    }
    fields[3] = trim(line);
    if (*fields[0] == '\0' || strlen(fields[3]) != 1)
        return STATUS_ERR_INPUT;
    switch (*fields[3]){
        case 'S': stop->marker = START; break;
        case 'I': stop->marker = INTERMEDIATE; break;
        case 'E': stop->marker = END; break;
        default: return STATUS_ERR_INPUT;
    }

    status_t st = parse_datetime(fields[1], &stop->arrival);
    if (st == STATUS_OK)
        st = parse_datetime(fields[2], &stop->departure);
    if (st != STATUS_OK)
        return st;
    if (stop->departure < stop->arrival)
        return STATUS_ERR_DATE;
    *id = fields[0];
    return STATUS_OK;
}

static void destroy_vehicles(Vehicle* vehicle){
    while (vehicle != NULL){
        Stop* stop = vehicle->stops;
        while (stop != NULL){
            Stop* next = stop->next;
            free(stop);
            stop = next;
        }
        Vehicle* next = vehicle->next;
        free(vehicle);
        vehicle = next;
    }
}

static status_t add_stop(Vehicle** vehicles, const char* id, const Stop* record){
    if (vehicles == NULL || id == NULL || record == NULL)
        return STATUS_ERR_NULL;
    if (*id == '\0')
        return STATUS_ERR_INPUT;

    Vehicle** link = vehicles;
    while (*link != NULL && strcmp((*link)->id, id) < 0)
        link = &(*link)->next;

    Stop* stop = malloc(sizeof(*stop));
    if (stop == NULL)
        return STATUS_ERR_ALLOC;
    if (*link == NULL || strcmp((*link)->id, id) != 0){
        const size_t len = strlen(id);
        if (len > SIZE_MAX - sizeof(Vehicle) - 1){
            free(stop);
            return STATUS_ERR_OVERFLOW;
        }
        Vehicle* vehicle = malloc(sizeof(*vehicle) + len + 1);
        if (vehicle == NULL){
            free(stop);
            return STATUS_ERR_ALLOC;
        }
        *vehicle = (Vehicle){0};
        memcpy(vehicle->id, id, len + 1);
        vehicle->next = *link;
        *link = vehicle;
    }

    *stop = *record;
    stop->next = (*link)->stops;
    (*link)->stops = stop;
    return STATUS_OK;
}

static int compare_distance(double a, double b, double epsilon){
    if (fpclassify(epsilon) == FP_ZERO)
        return (a > b) - (a < b);
    const double difference = a - b;
    return (difference > epsilon) - (difference < -epsilon);
}

static int stop_precedes(const Stop* a, const Stop* b){
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    if (a->departure != b->departure)
        return a->departure < b->departure;
    if (a->marker != b->marker)
        return a->marker < b->marker;
    int cmp = compare_distance(a->point.x, b->point.x, 0.0);
    return cmp != 0 ? cmp < 0 : compare_distance(a->point.y, b->point.y, 0.0) < 0;
}

static Stop* sort_stops(Stop* first){
    if (first == NULL || first->next == NULL)
        return first;
    Stop* middle = first;
    for (const Stop* fast = first->next; fast != NULL && fast->next != NULL;
         fast = fast->next->next)
        middle = middle->next;
    Stop* right = middle->next;
    middle->next = NULL;
    Stop* left = sort_stops(first);
    right = sort_stops(right);

    Stop* sorted = NULL;
    Stop** tail = &sorted;
    while (left != NULL && right != NULL){
        Stop** next = stop_precedes(right, left) ? &right : &left;
        *tail = *next;
        *next = (*next)->next;
        tail = &(*tail)->next;
    }
    *tail = left != NULL ? left : right;
    return sorted;
}

static int same_times(const Stop* a, const Stop* b){
    return a != NULL && a->arrival == b->arrival && a->departure == b->departure;
}

// Resolve an equal-time group by taking intermediates and then an end while a
// route is open, and a start otherwise. The sort orders each marker's queue.
static status_t order_boundaries(Stop** link){
    if (*link == NULL)
        return STATUS_ERR_ROUTE;
    int open = 0;
    while (*link != NULL){
        const Stop* first = *link;
        Stop* pending[END + 1] = {0};
        size_t count[END + 1] = {0};
        Stop* next = *link;
        while (same_times(next, first)){
            if (count[next->marker]++ == 0)
                pending[next->marker] = next;
            next = next->next;
        }
        size_t starts = count[START] + (size_t)open;
        if (count[END] > starts || starts - count[END] > 1 ||
            (starts == 0 && count[INTERMEDIATE] != 0))
            return STATUS_ERR_ROUTE;

        while (count[START] || count[INTERMEDIATE] || count[END]){
            marker_t marker = open ? (count[INTERMEDIATE] ? INTERMEDIATE : END) : START;
            Stop* stop = pending[marker];
            pending[marker] = stop->next;
            count[marker]--;
            *link = stop;
            link = &stop->next;
            open = marker != END;
        }
        *link = next;
    }
    return open ? STATUS_ERR_ROUTE : STATUS_OK;
}

static status_t summarize(const Stop* first, double epsilon, Stats* result){
    Stats stats = {0};
    const Stop* previous = NULL;
    double route_length = 0.0;

    for (const Stop* stop = first; stop != NULL; stop = stop->next){
        if (previous != NULL && previous->departure > stop->arrival)
            return STATUS_ERR_ROUTE;

        uintmax_t duration = stop->departure - stop->arrival;
        if (previous == NULL || duration < stats.shortest_stop)
            stats.shortest_stop = duration;
        if (duration > stats.longest_stop)
            stats.longest_stop = duration;
        // Nonoverlapping stays fit in the finite range of the timestamps.
        stats.total_stop += duration;

        if (stop->marker == START){
            route_length = 0.0;
        } else {
            route_length += hypot(stop->point.x - previous->point.x,
                                  stop->point.y - previous->point.y);
            if (!isfinite(route_length))
                return STATUS_ERR_OVERFLOW;
        }
        if (stop->marker == END){
            if (stats.routes == 0 || compare_distance(route_length, stats.shortest_route, epsilon) < 0)
                stats.shortest_route = route_length;
            if (stats.routes == 0 || compare_distance(route_length, stats.longest_route, epsilon) > 0)
                stats.longest_route = route_length;
            stats.routes++;
            stats.total_distance += route_length;
            if (!isfinite(stats.total_distance))
                return STATUS_ERR_OVERFLOW;
        }
        previous = stop;
    }
    *result = stats;
    return STATUS_OK;
}

static status_t prepare_vehicle(Vehicle* vehicle, double epsilon){
    if (vehicle == NULL)
        return STATUS_ERR_NULL;
    if (!isfinite(epsilon) || signbit(epsilon))
        return STATUS_ERR_INPUT;
    vehicle->stops = sort_stops(vehicle->stops);
    status_t st = order_boundaries(&vehicle->stops);
    return st == STATUS_OK ? summarize(vehicle->stops, epsilon, &vehicle->stats) : st;
}

static int is_distance_metric(metric_t metric){
    return metric == TOTAL_DISTANCE || metric == ROUTE_DISTANCE;
}

static Measure get_measure(const Stats* stats, metric_t metric, int maximum){
    switch (metric){
        case ROUTE_COUNT:         return (Measure){stats->routes, 0};
        case TOTAL_DISTANCE:      return (Measure){0, stats->total_distance};
        case ROUTE_DISTANCE:      return (Measure){0, maximum ? stats->longest_route : stats->shortest_route};
        case STOP_DURATION:       return (Measure){maximum ? stats->longest_stop : stats->shortest_stop, 0};
        case TOTAL_STOP_DURATION: return (Measure){stats->total_stop, 0};
    }
    return (Measure){0};
}

static status_t find_vehicle(const Vehicle* vehicles, metric_t metric, int maximum, double epsilon,
                             const Vehicle** result, Measure* value){
    if (result == NULL || value == NULL)
        return STATUS_ERR_NULL;
    if (!isfinite(epsilon) || signbit(epsilon) || metric < ROUTE_COUNT || metric > TOTAL_STOP_DURATION)
        return STATUS_ERR_INPUT;
    if (vehicles == NULL)
        return STATUS_ERR_EMPTY;

    const Vehicle* best = vehicles;
    Measure best_value = get_measure(&best->stats, metric, maximum);
    for (const Vehicle* v = vehicles->next; v != NULL; v = v->next){
        Measure candidate = get_measure(&v->stats, metric, maximum);
        int cmp = is_distance_metric(metric) ?
                  compare_distance(candidate.distance, best_value.distance, epsilon) :
                  (candidate.whole > best_value.whole) - (candidate.whole < best_value.whole);
        if (maximum ? cmp > 0 : cmp < 0){
            best = v;
            best_value = candidate;
        }
    }
    *result = best;
    *value = best_value;
    return STATUS_OK;
}

#endif
