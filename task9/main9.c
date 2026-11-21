#include "functions9.h"
#include <ctype.h>

static status_t prompt_line(const char* prompt, Buffer* line){
    if (fputs(prompt, stdout) == EOF || fflush(stdout) == EOF)
        return STATUS_ERR_WRITE;
    line->length = 0;
    status_t st = STATUS_OK;
    int c;
    while ((c = fgetc(stdin)) != EOF && c != '\n'){
        if (st != STATUS_OK)
            continue;
        unsigned char byte = (unsigned char)c;
        st = byte == 0 ? STATUS_ERR_INPUT : append_bytes(line, &byte, 1);
    }
    if (ferror(stdin))
        return STATUS_ERR_READ;
    if (st == STATUS_OK && c == EOF && line->length == 0)
        return STATUS_EOF;
    if (st == STATUS_OK && c == '\n' && line->length > 0 && line->data[line->length - 1] == '\r'){
        line->length--;
        line->data[line->length] = '\0';
    }
    return st;
}

static const char* line_text(const Buffer* line){
    return line->length == 0 ? "" : line->data;
}

static status_t parse_number(const char* text, size_t* result){
    while (isspace((unsigned char)*text))
        text++;
    if (*text < '0' || *text > '9')
        return STATUS_ERR_INPUT;
    size_t value = 0;
    for (; *text >= '0' && *text <= '9'; text++){
        size_t digit = (size_t)(*text - '0');
        if (value > (SIZE_MAX - digit) / 10)
            return STATUS_ERR_OVERFLOW;
        value = value * 10 + digit;
    }
    while (isspace((unsigned char)*text))
        text++;
    if (*text != '\0')
        return STATUS_ERR_INPUT;
    *result = value;
    return STATUS_OK;
}

static int hex_digit(unsigned char c){
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// A quoted query uses the same escapes as the printed words.
static status_t decode_query(Buffer* line){
    if (line->length == 0 || line->data[0] != '"')
        return STATUS_OK;
    unsigned char* data = (unsigned char*)line->data;
    size_t in = 1, out = 0;
    while (in < line->length){
        unsigned char c = data[in++];
        if (c == '"'){
            if (in != line->length)
                return STATUS_ERR_INPUT;
            data[out] = '\0';
            line->length = out;
            return STATUS_OK;
        }
        if (c == '\\'){
            switch (data[in++]){
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'x': {
                    if (line->length - in < 2)
                        return STATUS_ERR_INPUT;
                    int high = hex_digit(data[in++]);
                    int low = hex_digit(data[in++]);
                    if (high < 0 || low < 0 || (high == 0 && low == 0))
                        return STATUS_ERR_INPUT;
                    c = (unsigned char)(high * 16 + low);
                    break;
                }
                default: return STATUS_ERR_INPUT;
            }
        }
        data[out++] = c;
    }
    return STATUS_ERR_INPUT;
}

static status_t print_word(const char* word){
    if (putchar('"') == EOF)
        return STATUS_ERR_WRITE;
    for (const unsigned char* p = (const unsigned char*)word; *p != 0; p++){
        int written;
        switch (*p){
            case '\\': written = fputs("\\\\", stdout); break;
            case '"':  written = fputs("\\\"", stdout); break;
            case '\n': written = fputs("\\n", stdout);  break;
            case '\r': written = fputs("\\r", stdout);  break;
            case '\t': written = fputs("\\t", stdout);  break;
            default:
                written = *p < 0x20 || *p == 0x7f ? printf("\\x%02x", (unsigned int)*p) : putchar(*p);
        }
        if (written < 0)
            return STATUS_ERR_WRITE;
    }
    return putchar('"') == EOF ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t print_frequency(const WordNode* node){
    status_t st = print_word(node->word);
    if (st != STATUS_OK)
        return st;
    return printf(": %" PRIu64 "\n", node->frequency) < 0 ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t print_length(const char* label, const WordNode* node){
    if (fputs(label, stdout) == EOF)
        return STATUS_ERR_WRITE;
    status_t st = print_word(node->word);
    if (st != STATUS_OK)
        return st;
    return printf(" (%zu characters)\n", node->length) < 0 ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t print_empty(void){
    return puts("No words.") == EOF ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t file_command(WordNode** root, bool save, Buffer* line){
    status_t st = prompt_line(save ? "Save path: " : "Load path: ", line);
    if (st != STATUS_OK)
        return st;
    const char* path = line_text(line);
    st = save ? save_tree(path, *root) : load_tree(path, root);
    if (st != STATUS_OK)
        return fprintf(stderr, "Error: %s\n", status_message(st)) < 0 ? STATUS_ERR_WRITE : STATUS_OK;
    return puts(save ? "Tree saved." : "Tree loaded.") == EOF ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t run_command(size_t command, WordNode** root, Buffer* line){
    status_t st;
    switch (command){
        case 1:
            if (*root == NULL)
                return print_empty();
            for (const WordNode* node = first_word(*root); node != NULL; node = next_word(node)){
                st = print_frequency(node);
                if (st != STATUS_OK)
                    return st;
            }
            return STATUS_OK;
        case 2: {
            uint64_t frequency;
            st = prompt_line("Word (literal or quoted with escapes): ", line);
            if (st == STATUS_OK)
                st = decode_query(line);
            if (st == STATUS_OK)
                st = find_frequency(*root, line_text(line), &frequency);
            if (st != STATUS_OK)
                return st;
            return printf("Frequency: %" PRIu64 "\n", frequency) < 0 ? STATUS_ERR_WRITE : STATUS_OK;
        }
        case 3: {
            size_t n, count;
            const WordNode** top;
            st = prompt_line("Number of words: ", line);
            if (st == STATUS_OK)
                st = parse_number(line_text(line), &n);
            if (st == STATUS_OK)
                st = find_top(*root, n, &top, &count);
            if (st != STATUS_OK)
                return st;
            if (count == 0)
                st = print_empty();
            for (size_t i = 0; i < count && st == STATUS_OK; i++)
                st = print_frequency(top[i]);
            free(top);
            return st;
        }
        case 4: {
            const WordNode *shortest, *longest;
            st = find_extremes(*root, &shortest, &longest);
            if (st != STATUS_OK)
                return st;
            if (shortest == NULL)
                return print_empty();
            st = print_length("Shortest: ", shortest);
            return st == STATUS_OK ? print_length("Longest: ", longest) : st;
        }
        case 5: {
            size_t depth;
            st = tree_depth(*root, &depth);
            if (st != STATUS_OK)
                return st;
            return printf("Depth: %zu levels\n", depth) < 0 ? STATUS_ERR_WRITE : STATUS_OK;
        }
        case 6:
        case 7:
            return file_command(root, command == 6, line);
        default:
            return STATUS_ERR_INPUT;
    }
}

static status_t run_menu(WordNode** root){
    if (puts("1. All frequencies\n2. Find a word\n3. Top n words\n"
             "4. Shortest and longest words\n5. Tree depth (empty: 0, root: 1)\n"
             "6. Save tree\n7. Load tree\n0. Exit") == EOF)
        return STATUS_ERR_WRITE;
    Buffer line = {0};
    status_t st;
    for (;;){
        size_t command;
        st = prompt_line("> ", &line);
        if (st == STATUS_OK)
            st = parse_number(line_text(&line), &command);
        if (st == STATUS_OK && command == 0)
            break;
        if (st == STATUS_OK)
            st = run_command(command, root, &line);
        if (st == STATUS_EOF || st == STATUS_ERR_READ || st == STATUS_ERR_WRITE)
            break;
        if (st != STATUS_OK && fprintf(stderr, "Error: %s\n", status_message(st)) < 0){
            st = STATUS_ERR_WRITE;
            break;
        }
    }
    free(line.data);
    return st == STATUS_EOF ? STATUS_OK : st;
}

int main(int argc, char* argv[]){
    if (argc < 2 || argv[1][0] == '\0'){
        fprintf(stderr, "Usage: %s <text-file> [separator ...]\n", argc > 0 ? argv[0] : "main9");
        return EXIT_FAILURE;
    }
    int count = argc - 2;
    if (validate_separators(count, argv + 2) != STATUS_OK){
        fprintf(stderr, "Error: each separator must be one UTF-8 character.\n");
        return EXIT_FAILURE;
    }

    WordNode* root = NULL;
    FILE* file = fopen(argv[1], "rb");
    status_t st = file == NULL ? STATUS_ERR_OPEN : read_words(file, count, argv + 2, &root);
    if (file != NULL && fclose(file) == EOF && st == STATUS_OK)
        st = STATUS_ERR_CLOSE;
    if (st != STATUS_OK){
        fprintf(stderr, "Error: '%s': %s\n", argv[1], status_message(st));
    } else {
        st = run_menu(&root);
        if (fflush(stdout) == EOF && st == STATUS_OK)
            st = STATUS_ERR_WRITE;
        if (st != STATUS_OK)
            fprintf(stderr, "Error: %s\n", status_message(st));
    }
    destroy_tree(root);
    return st == STATUS_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
