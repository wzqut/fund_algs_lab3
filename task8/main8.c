#include "functions8.h"

typedef struct {
    char* data;
    size_t capacity, line;
} CommandReader;

static status_t read_command(FILE* in, CommandReader* reader, size_t* line_number){
    if (in == NULL || reader == NULL || line_number == NULL)
        return STATUS_ERR_NULL;
    size_t len = 0, comment_line = 0;
    int c, comment = 0, previous_cr = 0;
    *line_number = 0;
    while ((c = fgetc(in)) != EOF){
        if (c == '\n' && previous_cr){
            previous_cr = 0;
            continue;
        }
        previous_cr = c == '\r';
        if (previous_cr)
            c = '\n';
        size_t line = reader->line;
        if (c == '\n'){
            if (reader->line == SIZE_MAX)
                return STATUS_ERR_OVERFLOW;
            reader->line++;
        }
        if (c == '\0'){
            *line_number = line;
            return STATUS_ERR_INPUT;
        }
        if (comment == '%'){
            if (c == '\n')
                comment = 0;
            continue;
        }
        if ((comment == '[' && c == '[') || (comment == 0 && c == ']')){
            *line_number = line;
            return STATUS_ERR_COMMENT;
        }
        if (comment == '['){
            if (c == ']')
                comment = 0;
            continue;
        }
        // A comment separates tokens just like a space.
        if (c == '[' || c == '%'){
            comment = c;
            comment_line = line;
            c = ' ';
        }
        if (len == 0){
            if (isspace(c))
                continue;
            *line_number = line;
        }
        if (c == ';'){
            if (len == 0)
                return STATUS_ERR_INPUT;
            reader->data[len] = '\0';
            return STATUS_OK;
        }
        if (reader->capacity == 0 || len == reader->capacity - 1){
            if (reader->capacity == SIZE_MAX)
                return STATUS_ERR_OVERFLOW;
            size_t capacity = reader->capacity <= SIZE_MAX / 2 ? reader->capacity * 2 : SIZE_MAX;
            if (capacity == 0)
                capacity = 64;
            char* data = realloc(reader->data, capacity);
            if (data == NULL)
                return STATUS_ERR_ALLOC;
            reader->data = data;
            reader->capacity = capacity;
        }
        reader->data[len++] = (char)c;
    }
    if (ferror(in))
        return STATUS_ERR_READ;
    if (comment == '['){
        *line_number = comment_line;
        return STATUS_ERR_COMMENT;
    }
    return len == 0 ? STATUS_ERR_EOF : STATUS_ERR_TERMINATOR;
}

static status_t write_polynomial(FILE* out, const Polynomial* polynomial){
    if (out == NULL || polynomial == NULL)
        return STATUS_ERR_NULL;
    if (polynomial->head == NULL)
        return fputs("0\n", out) == EOF ? STATUS_ERR_WRITE : STATUS_OK;
    for (const Term* term = polynomial->head; term != NULL; term = term->next){
        int64_t magnitude = term->coefficient;
        if (magnitude < 0){
            if (fputc('-', out) == EOF)
                return STATUS_ERR_WRITE;
            magnitude = -magnitude;
        } else if (term != polynomial->head && fputc('+', out) == EOF){
            return STATUS_ERR_WRITE;
        }
        if ((magnitude != 1 || term->degree == 0) &&
            fprintf(out, "%" PRId64, magnitude) < 0)
            return STATUS_ERR_WRITE;
        if (term->degree != 0 && fputc('x', out) == EOF)
            return STATUS_ERR_WRITE;
        if (term->degree > 1 && fprintf(out, "^%" PRId32, term->degree) < 0)
            return STATUS_ERR_WRITE;
    }
    return fputc('\n', out) == EOF ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t execute_command(FILE* out, const Command* command, Polynomial* accumulator){
    if (out == NULL || command == NULL || accumulator == NULL)
        return STATUS_ERR_NULL;
    if (command->operation == OP_EVAL){
        const Polynomial* polynomial = command->use_accumulator ? accumulator : &command->first;
        long double value;
        status_t st = evaluate_polynomial(polynomial, command->point, &value);
        if (st != STATUS_OK)
            return st;
        return fprintf(out, "%.*Lg\n", DECIMAL_DIG, value) < 0 ? STATUS_ERR_WRITE : STATUS_OK;
    }
    Polynomial result = {0};
    status_t st = calculate_polynomial(command, accumulator, &result);
    if (st == STATUS_OK)
        st = write_polynomial(out, &result);
    if (st == STATUS_OK){
        destroy_polynomial(accumulator);
        *accumulator = result;
    } else {
        destroy_polynomial(&result);
    }
    return st;
}

static status_t run_commands(FILE* in, FILE* out, size_t* line_number){
    if (in == NULL || out == NULL || line_number == NULL)
        return STATUS_ERR_NULL;
    CommandReader reader = {.line = 1};
    Polynomial accumulator = {0};
    int has_commands = 0;
    status_t st;
    while ((st = read_command(in, &reader, line_number)) == STATUS_OK){
        Command command;
        st = parse_command(reader.data, &command);
        if (st != STATUS_OK)
            break;
        st = execute_command(out, &command, &accumulator);
        destroy_polynomial(&command.first);
        destroy_polynomial(&command.second);
        if (st != STATUS_OK)
            break;
        has_commands = 1;
    }
    destroy_polynomial(&accumulator);
    free(reader.data);
    if (st == STATUS_ERR_EOF)
        return has_commands ? STATUS_OK : STATUS_ERR_EMPTY;
    return st;
}

int main(int argc, char* argv[]){
    if (argc != 2 || argv[1][0] == '\0'){
        fprintf(stderr, "Usage: %s <command-file>\n", argc > 0 ? argv[0] : "main8");
        return EXIT_FAILURE;
    }

    FILE* file = fopen(argv[1], "r");
    size_t line_number = 0;
    status_t st = file == NULL ? STATUS_ERR_OPEN : run_commands(file, stdout, &line_number);
    if (file != NULL && fclose(file) == EOF && st == STATUS_OK)
        st = STATUS_ERR_CLOSE;
    if (fflush(stdout) == EOF && st == STATUS_OK)
        st = STATUS_ERR_WRITE;
    if (st != STATUS_OK){
        if (line_number != 0)
            fprintf(stderr, "Error: %s:%zu: %s\n", argv[1], line_number, status_message(st));
        else
            fprintf(stderr, "Error: '%s': %s\n", argv[1], status_message(st));
    }
    return st == STATUS_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
