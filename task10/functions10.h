#ifndef FUNCTIONS10_H
#define FUNCTIONS10_H

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    STATUS_OK,
    STATUS_EOF,
    STATUS_ERR_NULL,
    STATUS_ERR_FORMAT,
    STATUS_ERR_ALLOC,
    STATUS_ERR_OVERFLOW,
    STATUS_ERR_SAME_FILE,
    STATUS_ERR_OPEN_IN,
    STATUS_ERR_OPEN_OUT,
    STATUS_ERR_READ,
    STATUS_ERR_WRITE
} status_t;

typedef struct {
    unsigned char* data;
    size_t length;
    size_t capacity;
} Line;

typedef struct {
    size_t start;
    size_t length;
    size_t parent;
    size_t depth;
} TreeNode;

typedef struct {
    TreeNode* nodes;
    size_t count;
    size_t capacity;
} Tree;

static const char* status_message(status_t status){
    switch (status){
        case STATUS_OK:            return "ok";
        case STATUS_EOF:           return "end of input";
        case STATUS_ERR_NULL:      return "null pointer";
        case STATUS_ERR_FORMAT:    return "invalid tree expression";
        case STATUS_ERR_ALLOC:     return "memory allocation failed";
        case STATUS_ERR_OVERFLOW:  return "size is out of range";
        case STATUS_ERR_SAME_FILE: return "input and output refer to the same file";
        case STATUS_ERR_OPEN_IN:   return "cannot open input file";
        case STATUS_ERR_OPEN_OUT:  return "cannot open output file";
        case STATUS_ERR_READ:      return "cannot read input file";
        case STATUS_ERR_WRITE:     return "cannot write output file";
    }
    return "unknown error";
}

static status_t grow_array(void* data, size_t element_size, size_t* capacity, void** result){
    size_t limit = SIZE_MAX / element_size;
    if (*capacity == limit)
        return STATUS_ERR_OVERFLOW;

    size_t next = *capacity <= limit / 2 ? *capacity * 2 : limit;
    if (next < 64)
        next = limit < 64 ? limit : 64;
    void* grown = realloc(data, next * element_size);
    if (grown == NULL)
        return STATUS_ERR_ALLOC;

    *capacity = next;
    *result = grown;
    return STATUS_OK;
}

static status_t read_line(FILE* in, Line* line){
    line->length = 0;
    int c;
    while ((c = fgetc(in)) != EOF && c != '\n'){
        if (line->length == line->capacity){
            void* data;
            status_t st = grow_array(line->data, sizeof(*line->data), &line->capacity, &data);
            if (st != STATUS_OK)
                return st;
            line->data = data;
        }
        line->data[line->length++] = (unsigned char)c;
    }
    if (c != EOF)
        return STATUS_OK;
    if (ferror(in))
        return STATUS_ERR_READ;
    return line->length == 0 ? STATUS_EOF : STATUS_OK;
}

static size_t skip_space(const Line* line, size_t pos){
    while (pos < line->length && isspace(line->data[pos]))
        pos++;
    return pos;
}

static int is_label_byte(unsigned char c){
    return !isspace(c) && !iscntrl(c) && c != '(' && c != ')' && c != ',';
}

static status_t add_node(Tree* tree, size_t start, size_t length, size_t parent){
    if (tree->count == tree->capacity){
        void* nodes;
        status_t st = grow_array(tree->nodes, sizeof(*tree->nodes), &tree->capacity, &nodes);
        if (st != STATUS_OK)
            return st;
        tree->nodes = nodes;
    }

    size_t depth = parent == SIZE_MAX ? 0 : tree->nodes[parent].depth + 1;
    tree->nodes[tree->count++] = (TreeNode){start, length, parent, depth};
    return STATUS_OK;
}

static status_t parse_tree(const Line* line, Tree* tree){
    tree->count = 0;
    size_t parent = SIZE_MAX;
    size_t pos = 0;

    for (;;){
        size_t start = skip_space(line, pos);
        pos = start;
        while (pos < line->length && is_label_byte(line->data[pos]))
            pos++;
        if (pos == start)
            return STATUS_ERR_FORMAT;

        status_t st = add_node(tree, start, pos - start, parent);
        if (st != STATUS_OK)
            return st;

        pos = skip_space(line, pos);
        if (pos < line->length && line->data[pos] == '('){
            parent = tree->count - 1;
            pos++;
            continue;
        }

        while (pos < line->length && line->data[pos] == ')'){
            if (parent == SIZE_MAX)
                return STATUS_ERR_FORMAT;
            parent = tree->nodes[parent].parent;
            pos = skip_space(line, pos + 1);
        }

        if (pos == line->length)
            return parent == SIZE_MAX ? STATUS_OK : STATUS_ERR_FORMAT;
        if (line->data[pos] != ',' || parent == SIZE_MAX)
            return STATUS_ERR_FORMAT;
        pos++;
    }
}

static status_t write_tree(FILE* out, const Line* line, const Tree* tree){
    for (size_t i = 0; i < tree->count; i++){
        const TreeNode* node = &tree->nodes[i];
        for (size_t depth = 0; depth < node->depth; depth++){
            if (fputs("  ", out) == EOF)
                return STATUS_ERR_WRITE;
        }
        if (fwrite(line->data + node->start, 1, node->length, out) != node->length ||
            fputc('\n', out) == EOF)
            return STATUS_ERR_WRITE;
    }
    return fputc('\n', out) == EOF ? STATUS_ERR_WRITE : STATUS_OK;
}

static status_t process_stream(FILE* in, FILE* out, size_t* bad){
    if (in == NULL || out == NULL || bad == NULL)
        return STATUS_ERR_NULL;

    Line line = {0};
    Tree tree = {0};
    size_t number = 0;
    *bad = 0;

    status_t st;
    while ((st = read_line(in, &line)) == STATUS_OK){
        if (number == SIZE_MAX){
            st = STATUS_ERR_OVERFLOW;
            break;
        }
        number++;

        st = parse_tree(&line, &tree);
        if (st == STATUS_ERR_FORMAT){
            (*bad)++;
            if (fprintf(out, "Line %zu: %s\n\n", number, status_message(st)) < 0){
                st = STATUS_ERR_WRITE;
                break;
            }
            continue;
        }
        if (st != STATUS_OK)
            break;
        if (fprintf(out, "Line %zu:\n", number) < 0){
            st = STATUS_ERR_WRITE;
            break;
        }
        st = write_tree(out, &line, &tree);
        if (st != STATUS_OK)
            break;
    }

    free(tree.nodes);
    free(line.data);
    return st == STATUS_EOF ? STATUS_OK : st;
}

#endif
