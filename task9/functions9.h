#ifndef FUNCTIONS9_H
#define FUNCTIONS9_H

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK, STATUS_ERR_NULL, STATUS_ERR_ALLOC, STATUS_ERR_OVERFLOW,
    STATUS_ERR_INPUT, STATUS_ERR_UTF8, STATUS_EOF, STATUS_ERR_FORMAT,
    STATUS_ERR_OPEN, STATUS_ERR_READ, STATUS_ERR_WRITE, STATUS_ERR_CLOSE
} status_t;

typedef struct WordNode {
    struct WordNode* left, *right, *parent;
    uint64_t frequency;
    size_t bytes, length;
    char word[];
} WordNode;

typedef struct {
    char* data;
    size_t length, capacity;
} Buffer;

static const char* status_message(status_t st){
    switch (st){
        case STATUS_OK:           return "ok";
        case STATUS_ERR_NULL:     return "null pointer";
        case STATUS_ERR_ALLOC:    return "memory allocation failed";
        case STATUS_ERR_OVERFLOW: return "value is out of range";
        case STATUS_ERR_INPUT:    return "invalid command or argument";
        case STATUS_ERR_UTF8:     return "expected UTF-8 text without NUL bytes";
        case STATUS_EOF:          return "end of input";
        case STATUS_ERR_FORMAT:   return "invalid or incomplete tree file";
        case STATUS_ERR_OPEN:     return "cannot open file";
        case STATUS_ERR_READ:     return "cannot read input";
        case STATUS_ERR_WRITE:    return "cannot write output";
        case STATUS_ERR_CLOSE:    return "cannot close file";
    }
    return "unknown error";
}

static status_t append_bytes(Buffer* buffer, const void* bytes, size_t count){
    if (count >= SIZE_MAX - buffer->length)
        return STATUS_ERR_OVERFLOW;
    size_t needed = buffer->length + count + 1;
    if (needed > buffer->capacity){
        size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
        while (capacity < needed)
            capacity = capacity <= SIZE_MAX / 2 ? capacity * 2 : needed;
        char* data = realloc(buffer->data, capacity);
        if (data == NULL)
            return STATUS_ERR_ALLOC;
        buffer->data = data;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, bytes, count);
    buffer->length += count;
    buffer->data[buffer->length] = '\0';
    return STATUS_OK;
}

// Lengths count UTF-8 code points. Words keep their original bytes and case.
static size_t utf8_width(unsigned char first){
    if (first >= 0x01 && first <= 0x7f) return 1;
    if (first >= 0xc2 && first <= 0xdf) return 2;
    if (first >= 0xe0 && first <= 0xef) return 3;
    if (first >= 0xf0 && first <= 0xf4) return 4;
    return 0;
}

static bool valid_utf8_char(const unsigned char* bytes, size_t width){
    for (size_t i = 1; i < width; i++)
        if (bytes[i] < 0x80 || bytes[i] > 0xbf)
            return false;
    return width < 3 ||
           ((bytes[0] != 0xe0 || bytes[1] >= 0xa0) &&
            (bytes[0] != 0xed || bytes[1] <= 0x9f) &&
            (bytes[0] != 0xf0 || bytes[1] >= 0x90) &&
            (bytes[0] != 0xf4 || bytes[1] <= 0x8f));
}

static status_t utf8_length(const char* text, size_t bytes, size_t* length){
    const unsigned char* cursor = (const unsigned char*)text;
    size_t count = 0;
    while (bytes != 0){
        size_t width = utf8_width(*cursor);
        if (width == 0 || width > bytes || !valid_utf8_char(cursor, width))
            return STATUS_ERR_UTF8;
        cursor += width;
        bytes -= width;
        count++;
    }
    *length = count;
    return STATUS_OK;
}

static status_t read_utf8(FILE* in, unsigned char bytes[4], size_t* width){
    int c = fgetc(in);
    if (c == EOF)
        return ferror(in) ? STATUS_ERR_READ : STATUS_EOF;
    bytes[0] = (unsigned char)c;
    *width = utf8_width(bytes[0]);
    if (*width == 0)
        return STATUS_ERR_UTF8;
    for (size_t i = 1; i < *width; i++){
        c = fgetc(in);
        if (c == EOF)
            return ferror(in) ? STATUS_ERR_READ : STATUS_ERR_UTF8;
        bytes[i] = (unsigned char)c;
    }
    return valid_utf8_char(bytes, *width) ? STATUS_OK : STATUS_ERR_UTF8;
}

static status_t validate_separators(int count, char* const separators[]){
    for (int i = 0; i < count; i++){
        size_t length;
        if (utf8_length(separators[i], strlen(separators[i]), &length) != STATUS_OK || length != 1)
            return STATUS_ERR_INPUT;
    }
    return STATUS_OK;
}

static bool is_separator(const unsigned char* bytes, size_t width,
                         int count, char* const separators[]){
    for (int i = 0; i < count; i++)
        if (strncmp(separators[i], (const char*)bytes, width) == 0 && separators[i][width] == '\0')
            return true;
    return false;
}

static status_t make_node(const Buffer* word, size_t length, uint64_t frequency,
                          WordNode* parent, WordNode** result){
    if (word->length >= SIZE_MAX - sizeof(WordNode))
        return STATUS_ERR_OVERFLOW;
    WordNode* node = malloc(sizeof(*node) + word->length + 1);
    if (node == NULL)
        return STATUS_ERR_ALLOC;
    *node = (WordNode){NULL, NULL, parent, frequency, word->length, length};
    memcpy(node->word, word->data, word->length + 1);
    *result = node;
    return STATUS_OK;
}

static void destroy_tree(WordNode* node){
    while (node != NULL){
        if (node->left != NULL){
            node = node->left;
        } else if (node->right != NULL){
            node = node->right;
        } else {
            WordNode* parent = node->parent;
            if (parent != NULL){
                if (parent->left == node)
                    parent->left = NULL;
                else
                    parent->right = NULL;
            }
            free(node);
            node = parent;
        }
    }
}

static status_t add_word(WordNode** link, const Buffer* word, size_t length){
    WordNode* parent = NULL;
    while (*link != NULL){
        WordNode* node = *link;
        int cmp = strcmp(word->data, node->word);
        if (cmp == 0){
            if (node->frequency == UINT64_MAX)
                return STATUS_ERR_OVERFLOW;
            node->frequency++;
            return STATUS_OK;
        }
        parent = node;
        link = cmp < 0 ? &node->left : &node->right;
    }
    return make_node(word, length, 1, parent, link);
}

static status_t read_words(FILE* in, int count, char* const separators[], WordNode** tree){
    if (in == NULL || tree == NULL || (count != 0 && separators == NULL))
        return STATUS_ERR_NULL;
    Buffer word = {0};
    size_t length = 0, width;
    unsigned char bytes[4];
    status_t st;
    while ((st = read_utf8(in, bytes, &width)) == STATUS_OK){
        if (is_separator(bytes, width, count, separators)){
            if (length != 0)
                st = add_word(tree, &word, length);
            word.length = length = 0;
        } else {
            st = append_bytes(&word, bytes, width);
            length++;
        }
        if (st != STATUS_OK)
            break;
    }
    if (st == STATUS_EOF)
        st = length == 0 ? STATUS_OK : add_word(tree, &word, length);
    free(word.data);
    return st;
}

static status_t find_frequency(const WordNode* node, const char* word, uint64_t* frequency){
    if (word == NULL || frequency == NULL)
        return STATUS_ERR_NULL;
    if (*word == '\0')
        return STATUS_ERR_INPUT;
    size_t length;
    status_t st = utf8_length(word, strlen(word), &length);
    if (st != STATUS_OK)
        return st;
    while (node != NULL){
        int cmp = strcmp(word, node->word);
        if (cmp == 0)
            break;
        node = cmp < 0 ? node->left : node->right;
    }
    *frequency = node == NULL ? 0 : node->frequency;
    return STATUS_OK;
}

static const WordNode* first_word(const WordNode* node){
    while (node != NULL && node->left != NULL)
        node = node->left;
    return node;
}

static const WordNode* next_word(const WordNode* node){
    if (node->right != NULL)
        return first_word(node->right);
    while (node->parent != NULL && node == node->parent->right)
        node = node->parent;
    return node->parent;
}

static status_t find_extremes(const WordNode* root, const WordNode** shortest, const WordNode** longest){
    if (shortest == NULL || longest == NULL)
        return STATUS_ERR_NULL;
    *shortest = *longest = first_word(root);
    for (const WordNode* node = *shortest; node != NULL; node = next_word(node)){
        if (node->length < (*shortest)->length)
            *shortest = node;
        if (node->length > (*longest)->length)
            *longest = node;
    }
    return STATUS_OK;
}

static int compare_frequency(const void* first, const void* second){
    const WordNode* a = *(const WordNode* const*)first;
    const WordNode* b = *(const WordNode* const*)second;
    if (a->frequency != b->frequency)
        return a->frequency > b->frequency ? -1 : 1;
    return strcmp(a->word, b->word);
}

static status_t find_top(const WordNode* root, size_t n, const WordNode*** words, size_t* count){
    if (words == NULL || count == NULL)
        return STATUS_ERR_NULL;
    if (n == 0)
        return STATUS_ERR_INPUT;
    size_t size = 0;
    for (const WordNode* node = first_word(root); node != NULL; node = next_word(node)){
        if (size == SIZE_MAX / sizeof(**words))
            return STATUS_ERR_OVERFLOW;
        size++;
    }
    *words = NULL;
    *count = 0;
    if (size == 0)
        return STATUS_OK;
    const WordNode** sorted = malloc(size * sizeof(*sorted));
    if (sorted == NULL)
        return STATUS_ERR_ALLOC;
    size_t i = 0;
    for (const WordNode* node = first_word(root); node != NULL; node = next_word(node))
        sorted[i++] = node;
    qsort(sorted, size, sizeof(*sorted), compare_frequency);
    *words = sorted;
    *count = n < size ? n : size;
    return STATUS_OK;
}

typedef struct {
    WordNode** link;
    WordNode* parent;
    size_t depth;
} TreeSlot;

// Visit root, left and right slots in preorder, including empty slots.
static bool next_slot(TreeSlot* slot){
    if (*slot->link != NULL){
        slot->parent = *slot->link;
        slot->link = &slot->parent->left;
        slot->depth++;
        return true;
    }
    while (slot->parent != NULL && slot->link == &slot->parent->right){
        WordNode* child = slot->parent;
        slot->parent = child->parent;
        slot->depth--;
        if (slot->parent != NULL)
            slot->link = child == slot->parent->left ? &slot->parent->left : &slot->parent->right;
    }
    if (slot->parent == NULL)
        return false;
    slot->link = &slot->parent->right;
    return true;
}

static status_t tree_depth(WordNode* root, size_t* depth){
    if (depth == NULL)
        return STATUS_ERR_NULL;
    *depth = 0;
    TreeSlot slot = {&root, NULL, 0};
    do {
        if (*slot.link != NULL && slot.depth >= *depth)
            *depth = slot.depth + 1;
    } while (next_slot(&slot));
    return STATUS_OK;
}

// BST9 v1: tag 0 = empty, tag 1 = frequency, byte length, word, left, right.
// Integers use 8 little-endian bytes. The footer is the FNV-1a hash of the slots.
#define TREE_MAGIC "BST9\1"

typedef struct {
    FILE* file;
    uint64_t hash;
} TreeFile;

static void encode_u64(unsigned char bytes[8], uint64_t value){
    for (size_t i = 0; i < 8; i++){
        bytes[i] = (unsigned char)(value & 0xffu);
        value >>= 8;
    }
}

static uint64_t decode_u64(const unsigned char bytes[8]){
    uint64_t value = 0;
    for (size_t i = 8; i > 0; i--)
        value = (value << 8) | bytes[i - 1];
    return value;
}

static void hash_bytes(TreeFile* stream, const void* data, size_t size){
    const unsigned char* bytes = data;
    for (size_t i = 0; i < size; i++)
        stream->hash = (stream->hash ^ bytes[i]) * UINT64_C(1099511628211);
}

static status_t write_record(TreeFile* stream, const WordNode* node){
    unsigned char header[17] = {0};
    size_t size = 1;
    if (node != NULL){
        if ((uint64_t)node->bytes != node->bytes)
            return STATUS_ERR_OVERFLOW;
        header[0] = 1;
        encode_u64(header + 1, node->frequency);
        encode_u64(header + 9, (uint64_t)node->bytes);
        size = sizeof(header);
    }
    if (fwrite(header, 1, size, stream->file) != size)
        return STATUS_ERR_WRITE;
    hash_bytes(stream, header, size);
    if (node != NULL){
        if (fwrite(node->word, 1, node->bytes, stream->file) != node->bytes)
            return STATUS_ERR_WRITE;
        hash_bytes(stream, node->word, node->bytes);
    }
    return STATUS_OK;
}

static status_t write_tree(FILE* file, WordNode* root){
    if (fputs(TREE_MAGIC, file) == EOF)
        return STATUS_ERR_WRITE;
    TreeFile stream = {file, UINT64_C(14695981039346656037)};
    TreeSlot slot = {&root, NULL, 0};
    do {
        status_t st = write_record(&stream, *slot.link);
        if (st != STATUS_OK)
            return st;
    } while (next_slot(&slot));
    unsigned char footer[8];
    encode_u64(footer, stream.hash);
    return fwrite(footer, 1, sizeof(footer), file) == sizeof(footer) ? STATUS_OK : STATUS_ERR_WRITE;
}

static status_t read_bytes(FILE* file, void* data, size_t size){
    if (fread(data, 1, size, file) == size)
        return STATUS_OK;
    return ferror(file) ? STATUS_ERR_READ : STATUS_ERR_FORMAT;
}

static status_t read_record(TreeFile* stream, TreeSlot* slot, Buffer* word){
    unsigned char header[17];
    status_t st = read_bytes(stream->file, header, 1);
    if (st != STATUS_OK)
        return st;
    if (header[0] == 0){
        hash_bytes(stream, header, 1);
        return STATUS_OK;
    }
    if (header[0] != 1)
        return STATUS_ERR_FORMAT;
    st = read_bytes(stream->file, header + 1, sizeof(header) - 1);
    if (st != STATUS_OK)
        return st;
    hash_bytes(stream, header, sizeof(header));
    uint64_t frequency = decode_u64(header + 1), bytes = decode_u64(header + 9);
    if (frequency == 0 || bytes == 0 || bytes >= SIZE_MAX - sizeof(WordNode))
        return STATUS_ERR_FORMAT;

    // Grow the buffer only for bytes present in the file.
    word->length = 0;
    size_t remaining = (size_t)bytes;
    while (remaining != 0){
        char chunk[4096];
        size_t size = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        st = read_bytes(stream->file, chunk, size);
        if (st == STATUS_OK)
            st = append_bytes(word, chunk, size);
        if (st != STATUS_OK)
            return st;
        hash_bytes(stream, chunk, size);
        remaining -= size;
    }
    size_t length;
    if (utf8_length(word->data, word->length, &length) != STATUS_OK)
        return STATUS_ERR_FORMAT;
    return make_node(word, length, frequency, slot->parent, slot->link);
}

static status_t read_tree(FILE* file, WordNode** root){
    unsigned char magic[sizeof(TREE_MAGIC) - 1];
    status_t st = read_bytes(file, magic, sizeof(magic));
    if (st != STATUS_OK)
        return st;
    if (memcmp(magic, TREE_MAGIC, sizeof(magic)) != 0)
        return STATUS_ERR_FORMAT;

    TreeFile stream = {file, UINT64_C(14695981039346656037)};
    TreeSlot slot = {root, NULL, 0};
    Buffer word = {0};
    do {
        st = read_record(&stream, &slot, &word);
    } while (st == STATUS_OK && next_slot(&slot));
    free(word.data);
    if (st != STATUS_OK)
        return st;

    unsigned char footer[8];
    st = read_bytes(file, footer, sizeof(footer));
    if (st != STATUS_OK)
        return st;
    if (decode_u64(footer) != stream.hash || fgetc(file) != EOF)
        return STATUS_ERR_FORMAT;
    if (ferror(file))
        return STATUS_ERR_READ;
    const WordNode* previous = NULL;
    for (const WordNode* node = first_word(*root); node != NULL; node = next_word(node)){
        if (previous != NULL && strcmp(previous->word, node->word) >= 0)
            return STATUS_ERR_FORMAT;
        previous = node;
    }
    return STATUS_OK;
}

static status_t save_tree(const char* path, WordNode* root){
    if (path == NULL)
        return STATUS_ERR_NULL;
    if (*path == '\0')
        return STATUS_ERR_INPUT;
    FILE* file = fopen(path, "wb");
    if (file == NULL)
        return STATUS_ERR_OPEN;
    status_t st = write_tree(file, root);
    if (fflush(file) == EOF && st == STATUS_OK)
        st = STATUS_ERR_WRITE;
    if (fclose(file) == EOF && st == STATUS_OK)
        st = STATUS_ERR_CLOSE;
    return st;
}

static status_t load_tree(const char* path, WordNode** tree){
    if (path == NULL || tree == NULL)
        return STATUS_ERR_NULL;
    if (*path == '\0')
        return STATUS_ERR_INPUT;
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return STATUS_ERR_OPEN;
    WordNode* root = NULL;
    status_t st = read_tree(file, &root);
    if (fclose(file) == EOF && st == STATUS_OK)
        st = STATUS_ERR_CLOSE;
    if (st == STATUS_OK){
        destroy_tree(*tree);
        *tree = root;
    } else {
        destroy_tree(root);
    }
    return st;
}

#endif
