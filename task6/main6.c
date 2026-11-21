#include "functions6.h"

typedef struct {
    metric_t metric;
    int maximum;
    const char* label;
} Query;

static status_t load_stop_file(const char* path, Vehicle** vehicles, size_t* line_number){
    if (path == NULL || vehicles == NULL || line_number == NULL)
        return STATUS_ERR_NULL;
    *line_number = 0;
    if (*path == '\0')
        return STATUS_ERR_INPUT;
    FILE* file = fopen(path, "r");
    if (file == NULL)
        return STATUS_ERR_OPEN;

    Line line = {0};
    Point point;
    *line_number = 1;
    status_t st = read_line(file, &line);
    if (st == STATUS_ERR_EOF)
        st = STATUS_ERR_INPUT;
    if (st == STATUS_OK)
        st = parse_point(line.data, &point);
    while (st == STATUS_OK){
        st = read_line(file, &line);
        if (st == STATUS_ERR_EOF){
            st = STATUS_OK;
            break;
        }
        if (*line_number == SIZE_MAX){
            st = STATUS_ERR_OVERFLOW;
            break;
        }
        (*line_number)++;
        if (st != STATUS_OK)
            break;
        if (*trim(line.data) == '\0')
            continue;

        const char* id;
        Stop record = {.point = point};
        st = parse_record(line.data, &id, &record);
        if (st == STATUS_OK)
            st = add_stop(vehicles, id, &record);
    }
    free(line.data);
    if (fclose(file) == EOF && st == STATUS_OK){
        *line_number = 0;
        st = STATUS_ERR_CLOSE;
    }
    return st;
}

static status_t write_result(const Vehicle* vehicles, const Query* query, double epsilon){
    const Vehicle* best;
    Measure value;
    status_t st = find_vehicle(vehicles, query->metric, query->maximum, epsilon, &best, &value);
    if (st != STATUS_OK)
        return st;
    int written;
    if (is_distance_metric(query->metric))
        written = printf("Vehicle: %s (%s: %.15g)\n", best->id, query->label, value.distance);
    else
        written = printf("Vehicle: %s (%s: %ju%s)\n", best->id, query->label, value.whole,
                         query->metric == ROUTE_COUNT ? "" : " s");
    return written < 0 ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t run_dialog(const Vehicle* vehicles, double epsilon){
    const Query queries[] = {
        {ROUTE_COUNT,         1, "most routes"},
        {ROUTE_COUNT,         0, "fewest routes"},
        {TOTAL_DISTANCE,      1, "longest total distance"},
        {TOTAL_DISTANCE,      0, "shortest total distance"},
        {ROUTE_DISTANCE,      1, "longest route"},
        {ROUTE_DISTANCE,      0, "shortest route"},
        {STOP_DURATION,       1, "longest stop"},
        {STOP_DURATION,       0, "shortest stop"},
        {TOTAL_STOP_DURATION, 1, "longest total stop duration"}
    };
    const size_t count = sizeof(queries) / sizeof(*queries);
    Line line = {0};
    status_t st = STATUS_OK;

    while (st == STATUS_OK){
        if (puts("\n[Transport]") == EOF){
            st = STATUS_ERR_WRITE;
            break;
        }
        for (size_t i = 0; i < count && st == STATUS_OK; i++)
            if (printf("%zu - %s\n", i + 1, queries[i].label) < 0)
                st = STATUS_ERR_WRITE;
        if (st != STATUS_OK || fputs("0 - exit\nYour choice: ", stdout) == EOF ||
            fflush(stdout) == EOF){
            st = STATUS_ERR_WRITE;
            break;
        }

        st = read_line(stdin, &line);
        if (st == STATUS_OK){
            const char* choice = trim(line.data);
            if (strcmp(choice, "0") == 0)
                break;
            if (*choice < '1' || *choice > '9' || choice[1] != '\0' ||
                (size_t)(*choice - '1') >= count)
                st = STATUS_ERR_INPUT;
            else
                st = write_result(vehicles, &queries[*choice - '1'], epsilon);
        }
        if (st == STATUS_ERR_INPUT){
            fprintf(stderr, "Error: %s\n", status_message(st));
            st = STATUS_OK;
        }
    }
    free(line.data);
    return st == STATUS_ERR_EOF ? STATUS_OK : st;
}

int main(int argc, char* argv[]){
    if (argc < 2){
        fprintf(stderr, "Usage: %s <stop-file> [<stop-file> ...]\n", argc > 0 ? argv[0] : "main6");
        return EXIT_FAILURE;
    }

    const double epsilon = 1e-9;
    Vehicle* vehicles = NULL;
    status_t st = STATUS_OK;
    for (int i = 1; i < argc && st == STATUS_OK; i++){
        size_t line_number = 0;
        st = load_stop_file(argv[i], &vehicles, &line_number);
        if (st != STATUS_OK){
            if (line_number != 0)
                fprintf(stderr, "Error: %s:%zu: %s\n", argv[i], line_number, status_message(st));
            else
                fprintf(stderr, "Error: '%s': %s\n", argv[i], status_message(st));
        }
    }
    for (Vehicle* v = vehicles; v != NULL && st == STATUS_OK; v = v->next){
        st = prepare_vehicle(v, epsilon);
        if (st != STATUS_OK)
            fprintf(stderr, "Error: vehicle %s: %s\n", v->id, status_message(st));
    }
    if (st == STATUS_OK){
        st = vehicles == NULL ? STATUS_ERR_EMPTY : run_dialog(vehicles, epsilon);
        if (fflush(stdout) == EOF && st == STATUS_OK)
            st = STATUS_ERR_WRITE;
        if (st != STATUS_OK)
            fprintf(stderr, "Error: %s\n", status_message(st));
    }

    destroy_vehicles(vehicles);
    return st == STATUS_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
