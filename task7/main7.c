#include "functions7.h"

static status_t load_registry(const char* path, Registry* registry, size_t* line_number){
    if (path == NULL || registry == NULL || line_number == NULL)
        return STATUS_ERR_NULL;
    *line_number = 0;
    if (*path == '\0')
        return STATUS_ERR_INPUT;
    FILE* file = fopen(path, "r");
    if (file == NULL)
        return STATUS_ERR_OPEN;

    Line line = {0};
    status_t st = STATUS_OK;
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

        char* text = trim(line.data);
        if (*text == '\0')
            continue;
        Liver data;
        st = parse_liver(text, 0, &data);
        if (st == STATUS_OK && registry->last_id == SIZE_MAX)
            st = STATUS_ERR_OVERFLOW;
        if (st == STATUS_OK){
            Resident* node;
            st = make_resident(&data, registry->last_id + 1, &node);
            if (st == STATUS_OK){
                registry->last_id = node->id;
                insert_resident(registry, node);
            }
        }
    }
    free(line.data);
    if (fclose(file) == EOF && st == STATUS_OK){
        *line_number = 0;
        st = STATUS_ERR_CLOSE;
    }
    return st;
}

static status_t write_liver(FILE* out, const Liver* data){
    if (out == NULL || data == NULL)
        return STATUS_ERR_NULL;
    return fprintf(out, "%s;%s;%s;%02u.%02u.%04u;%c;%.*g\n",
                   data->surname, data->name, data->patronymic, data->birth.day,
                   data->birth.month, data->birth.year, data->sex,
                   DECIMAL_DIG, data->income) < 0 ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t export_registry(const char* path, const Registry* registry){
    if (path == NULL || registry == NULL)
        return STATUS_ERR_NULL;
    if (*path == '\0')
        return STATUS_ERR_INPUT;
    FILE* file = fopen(path, "w");
    if (file == NULL)
        return STATUS_ERR_OPEN;

    status_t st = STATUS_OK;
    for (const Resident* node = registry->head; node != NULL && st == STATUS_OK; node = node->next)
        st = write_liver(file, &node->data);
    if (fflush(file) == EOF && st == STATUS_OK)
        st = STATUS_ERR_WRITE;
    if (fclose(file) == EOF && st == STATUS_OK)
        st = STATUS_ERR_CLOSE;
    return st;
}

static status_t prompt_line(const char* prompt, Line* line){
    if (prompt == NULL || line == NULL)
        return STATUS_ERR_NULL;
    if (fputs(prompt, stdout) == EOF || fflush(stdout) == EOF)
        return STATUS_ERR_WRITE;
    return read_line(stdin, line);
}

static status_t list_residents(const Registry* registry, const Liver* query, double epsilon){
    if (registry == NULL)
        return STATUS_ERR_NULL;
    int found = 0;
    const Resident* node = registry->head;
    for (;;){
        status_t st = find_resident(node, query, epsilon, &node);
        if (st != STATUS_OK)
            return st;
        if (node == NULL)
            break;
        found = 1;
        if (printf("ID %zu  ", node->id) < 0)
            return STATUS_ERR_WRITE;
        st = write_liver(stdout, &node->data);
        if (st != STATUS_OK)
            return st;
        node = node->next;
    }
    return !found && puts("No matching residents") == EOF ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t action_search(const Registry* registry, Line* line, double epsilon){
    status_t st = prompt_line("Enter surname;name;patronymic;dd.MM.yyyy;M/W;income\n"
                              "Use * for any value. An empty patronymic matches empty patronymics.\n"
                              "Search: ", line);
    Liver query;
    if (st == STATUS_OK)
        st = parse_liver(line->data, 1, &query);
    return st == STATUS_OK ? list_residents(registry, &query, epsilon) : st;
}

static status_t action_change(Registry* registry, Line* line, char choice){
    size_t id = 0;
    status_t st = STATUS_OK;
    if (choice != '3'){
        st = prompt_line("Resident ID: ", line);
        if (st == STATUS_OK)
            st = parse_id(trim(line->data), &id);
    }
    if (st != STATUS_OK)
        return st;

    Liver data;
    if (choice != '4'){
        if (id != 0 && *resident_link(registry, id) == NULL)
            return STATUS_ERR_NOT_FOUND;
        st = prompt_line("Enter surname;name;patronymic;dd.MM.yyyy;M/W;income\n"
                         "Record: ", line);
        if (st == STATUS_OK)
            st = parse_liver(line->data, 0, &data);
    }
    if (st == STATUS_OK)
        st = change_resident(registry, id, choice == '4' ? NULL : &data);
    if (st != STATUS_OK)
        return st;

    if (choice == '3')
        return printf("Resident added (ID %zu)\n", registry->last_id) < 0 ? STATUS_ERR_WRITE : STATUS_OK;
    const char* message = choice == '2' ? "Resident updated" : "Resident deleted";
    return puts(message) == EOF ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t action_export(const Registry* registry, Line* line){
    status_t st = prompt_line("Output file: ", line);
    if (st != STATUS_OK)
        return st;
    st = export_registry(line->data, registry);
    if (st == STATUS_OK)
        return puts("Residents exported") == EOF ? STATUS_ERR_WRITE : STATUS_OK;
    return fprintf(stderr, "Error: '%s': %s\n", line->data, status_message(st)) < 0
           ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t action_undo(Registry* registry){
    size_t undone;
    status_t st = undo_changes(registry, &undone);
    if (st != STATUS_OK)
        return st;
    int written = undone == 0 ? puts("Nothing to undo") : printf("Changes undone: %zu\n", undone);
    return written < 0 ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t run_dialog(Registry* registry, double epsilon){
    if (registry == NULL)
        return STATUS_ERR_NULL;
    const char* menu = "\n[Residents]\n"
                       "1 - search\n2 - edit\n3 - add\n4 - delete\n"
                       "5 - export\n6 - undo\n7 - list all\n0 - exit\nYour choice: ";
    Line line = {0};
    status_t st = STATUS_OK;

    while (st == STATUS_OK){
        st = prompt_line(menu, &line);
        if (st == STATUS_OK){
            const char* choice = trim(line.data);
            if (choice[0] == '\0' || choice[1] != '\0'){
                st = STATUS_ERR_INPUT;
            } else if (*choice == '0'){
                break;
            } else {
                switch (*choice){
                    case '1': st = action_search(registry, &line, epsilon); break;
                    case '2':
                    case '3':
                    case '4': st = action_change(registry, &line, *choice); break;
                    case '5': st = action_export(registry, &line); break;
                    case '6': st = action_undo(registry); break;
                    case '7': st = list_residents(registry, NULL, epsilon); break;
                    default:  st = STATUS_ERR_INPUT; break;
                }
            }
        }
        if (st == STATUS_ERR_INPUT || st == STATUS_ERR_DATE ||
            st == STATUS_ERR_NOT_FOUND || st == STATUS_ERR_OVERFLOW){
            st = fprintf(stderr, "Error: %s\n", status_message(st)) < 0 ? STATUS_ERR_WRITE : STATUS_OK;
        }
    }
    free(line.data);
    return st == STATUS_ERR_EOF ? STATUS_OK : st;
}

int main(int argc, char* argv[]){
    if (argc != 2){
        fprintf(stderr, "Usage: %s <residents-file>\n", argc > 0 ? argv[0] : "main7");
        return EXIT_FAILURE;
    }

    const double epsilon = 1e-9;
    Registry registry = {0};
    size_t line_number = 0;
    status_t st = load_registry(argv[1], &registry, &line_number);
    if (st != STATUS_OK){
        if (line_number != 0)
            fprintf(stderr, "Error: %s:%zu: %s\n", argv[1], line_number, status_message(st));
        else
            fprintf(stderr, "Error: '%s': %s\n", argv[1], status_message(st));
    } else {
        st = run_dialog(&registry, epsilon);
        if (fflush(stdout) == EOF && st == STATUS_OK)
            st = STATUS_ERR_WRITE;
        if (st != STATUS_OK)
            fprintf(stderr, "Error: %s\n", status_message(st));
    }

    destroy_registry(&registry);
    return st == STATUS_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
