#ifndef FUNCTIONS8_H
#define FUNCTIONS8_H

#include <ctype.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STATUS_OK,
    STATUS_ERR_NULL,
    STATUS_ERR_ALLOC,
    STATUS_ERR_OVERFLOW,
    STATUS_ERR_INPUT,
    STATUS_ERR_EMPTY,
    STATUS_ERR_COMMENT,
    STATUS_ERR_TERMINATOR,
    STATUS_ERR_DIV_ZERO,
    STATUS_ERR_EOF,
    STATUS_ERR_OPEN,
    STATUS_ERR_READ,
    STATUS_ERR_WRITE,
    STATUS_ERR_CLOSE
} status_t;

typedef struct Term {
    int32_t degree, coefficient;
    struct Term* next;
} Term;

typedef struct {
    Term* head;
} Polynomial;

typedef enum {
    OP_ADD, OP_SUB, OP_MULT, OP_DIV, OP_MOD, OP_EVAL, OP_DIFF, OP_CMPS
} operation_t;

typedef struct {
    operation_t operation;
    Polynomial first, second;
    int use_accumulator;
    long double point;
} Command;

static const char* status_message(status_t st){
    switch (st){
        case STATUS_OK:             return "ok";
        case STATUS_ERR_NULL:       return "null pointer";
        case STATUS_ERR_ALLOC:      return "memory allocation failed";
        case STATUS_ERR_OVERFLOW:   return "value is out of range";
        case STATUS_ERR_INPUT:      return "invalid command or argument";
        case STATUS_ERR_EMPTY:      return "no commands in input";
        case STATUS_ERR_COMMENT:    return "invalid or unclosed block comment";
        case STATUS_ERR_TERMINATOR: return "missing ';' at end of command";
        case STATUS_ERR_DIV_ZERO:   return "division by the zero polynomial";
        case STATUS_ERR_EOF:        return "end of input";
        case STATUS_ERR_OPEN:       return "cannot open file";
        case STATUS_ERR_READ:       return "cannot read input";
        case STATUS_ERR_WRITE:      return "cannot write output";
        case STATUS_ERR_CLOSE:      return "cannot close file";
    }
    return "unknown error";
}

static void destroy_polynomial(Polynomial* polynomial){
    if (polynomial == NULL)
        return;
    while (polynomial->head != NULL){
        Term* term = polynomial->head;
        polynomial->head = term->next;
        free(term);
    }
}

// Update the slot for this degree in a descending list.
static status_t add_term(Term** link, int32_t degree, int64_t coefficient){
    if (link == NULL)
        return STATUS_ERR_NULL;
    Term* term = *link;
    int exists = term != NULL && term->degree == degree;
    if (exists)
        coefficient += term->coefficient;
    if (coefficient < INT32_MIN || coefficient > INT32_MAX)
        return STATUS_ERR_OVERFLOW;
    if (exists){
        if (coefficient == 0){
            *link = term->next;
            free(term);
        } else {
            term->coefficient = (int32_t)coefficient;
        }
    } else if (coefficient != 0){
        term = malloc(sizeof(*term));
        if (term == NULL)
            return STATUS_ERR_ALLOC;
        *term = (Term){degree, (int32_t)coefficient, *link};
        *link = term;
    }
    return STATUS_OK;
}

// Add factor * x^shift * source to a separate list.
static status_t add_scaled(Polynomial* target, const Polynomial* source, int64_t factor,
                           int32_t shift){
    if (target == NULL || source == NULL)
        return STATUS_ERR_NULL;
    Term** link = &target->head;
    for (const Term* term = source->head; term != NULL; term = term->next){
        if (term->degree > INT32_MAX - shift)
            return STATUS_ERR_OVERFLOW;
        int32_t degree = term->degree + shift;
        while (*link != NULL && (*link)->degree > degree)
            link = &(*link)->next;
        status_t st = add_term(link, degree, factor * term->coefficient);
        if (st != STATUS_OK)
            return st;
        if (*link != NULL && (*link)->degree == degree)
            link = &(*link)->next;
    }
    return STATUS_OK;
}

static status_t add_polynomials(const Polynomial* first, const Polynomial* second,
                                int subtract, Polynomial* result){
    if (first == NULL || second == NULL || result == NULL)
        return STATUS_ERR_NULL;
    Polynomial sum = {0};
    status_t st = add_scaled(&sum, first, 1, 0);
    if (st == STATUS_OK)
        st = add_scaled(&sum, second, subtract ? -1 : 1, 0);
    if (st == STATUS_OK)
        *result = sum;
    else
        destroy_polynomial(&sum);
    return st;
}

static status_t multiply_polynomials(const Polynomial* first, const Polynomial* second,
                                     Polynomial* result){
    if (first == NULL || second == NULL || result == NULL)
        return STATUS_ERR_NULL;
    Polynomial product = {0};
    status_t st = STATUS_OK;
    if (second->head != NULL)
        for (const Term* term = first->head; term != NULL && st == STATUS_OK; term = term->next)
            st = add_scaled(&product, second, term->coefficient, term->degree);
    if (st == STATUS_OK)
        *result = product;
    else
        destroy_polynomial(&product);
    return st;
}

static status_t divide_polynomials(const Polynomial* dividend, const Polynomial* divisor,
                                   Polynomial* quotient, Polynomial* remainder){
    if (dividend == NULL || divisor == NULL || quotient == NULL || remainder == NULL)
        return STATUS_ERR_NULL;
    const Term* lead = divisor->head;
    if (lead == NULL)
        return STATUS_ERR_DIV_ZERO;

    Polynomial q = {0}, r = {0};
    Term** tail = &q.head;
    status_t st = add_scaled(&r, dividend, 1, 0);
    while (st == STATUS_OK && r.head != NULL && r.head->degree >= lead->degree){
        int32_t shift = r.head->degree - lead->degree;
        int64_t factor = (int64_t)r.head->coefficient / lead->coefficient;
        if (factor == 0)
            break;
        st = add_term(tail, shift, factor);
        if (st == STATUS_OK){
            tail = &(*tail)->next;
            st = add_scaled(&r, divisor, -factor, shift);
        }
    }
    if (st == STATUS_OK){
        *quotient = q;
        *remainder = r;
    } else {
        destroy_polynomial(&q);
        destroy_polynomial(&r);
    }
    return st;
}

static status_t differentiate_polynomial(const Polynomial* polynomial, Polynomial* result){
    if (polynomial == NULL || result == NULL)
        return STATUS_ERR_NULL;
    Polynomial derivative = {0};
    Term** tail = &derivative.head;
    for (const Term* term = polynomial->head; term != NULL && term->degree != 0; term = term->next){
        status_t st = add_term(tail, term->degree - 1, (int64_t)term->coefficient * term->degree);
        if (st != STATUS_OK){
            destroy_polynomial(&derivative);
            return st;
        }
        tail = &(*tail)->next;
    }
    *result = derivative;
    return STATUS_OK;
}

static status_t multiply_by_power(Polynomial* result, const Polynomial* base, int32_t degree){
    if (result == NULL || base == NULL)
        return STATUS_ERR_NULL;
    Polynomial power = {0};
    const Polynomial* factor = base;
    status_t st = STATUS_OK;
    // Keep the base borrowed until the first square.
    while (degree != 0 && result->head != NULL && st == STATUS_OK){
        if (degree % 2 != 0){
            Polynomial product;
            st = multiply_polynomials(result, factor, &product);
            if (st == STATUS_OK){
                destroy_polynomial(result);
                *result = product;
            }
        }
        degree /= 2;
        if (degree != 0 && result->head != NULL && st == STATUS_OK){
            Polynomial square;
            st = multiply_polynomials(factor, factor, &square);
            if (st == STATUS_OK){
                destroy_polynomial(&power);
                power = square;
                factor = &power;
            }
        }
    }
    destroy_polynomial(&power);
    return st;
}

static status_t compose_polynomials(const Polynomial* outer, const Polynomial* inner,
                                    Polynomial* result){
    if (outer == NULL || inner == NULL || result == NULL)
        return STATUS_ERR_NULL;
    Polynomial composition = {0};
    status_t st = STATUS_OK;
    // Horner's rule with powers for gaps between stored degrees.
    for (const Term* term = outer->head; term != NULL && st == STATUS_OK; term = term->next){
        Term** constant = &composition.head;
        while (*constant != NULL && (*constant)->degree != 0)
            constant = &(*constant)->next;
        st = add_term(constant, 0, term->coefficient);
        if (st == STATUS_OK){
            int32_t next_degree = term->next == NULL ? 0 : term->next->degree;
            st = multiply_by_power(&composition, inner, term->degree - next_degree);
        }
    }
    if (st == STATUS_OK)
        *result = composition;
    else
        destroy_polynomial(&composition);
    return st;
}

// Track the binary exponent separately so the power itself cannot overflow or underflow.
static long double scale_at_point(long double value, long double point, int32_t degree){
    if (degree == 0)
        return value;
    if (fpclassify(value) == FP_ZERO || fpclassify(point) == FP_ZERO)
        return 0.0L;

    int exponent, base_exponent;
    value = frexpl(value, &exponent);
    point = frexpl(point, &base_exponent);
    int64_t total = exponent, power = base_exponent;
    while (degree != 0){
        if (degree % 2 != 0){
            value = frexpl(value * point, &exponent);
            total += power + exponent;
        }
        degree /= 2;
        if (degree != 0){
            point = frexpl(point * point, &exponent);
            power = 2 * power + exponent;
        }
    }
    if (total > INT_MAX)
        return copysignl(HUGE_VALL, value);
    if (total < INT_MIN)
        return copysignl(0.0L, value);
    return ldexpl(value, (int)total);
}

static status_t evaluate_polynomial(const Polynomial* polynomial, long double point,
                                    long double* result){
    if (polynomial == NULL || result == NULL)
        return STATUS_ERR_NULL;
    if (!isfinite(point))
        return STATUS_ERR_OVERFLOW;
    long double value = 0.0L;
    for (const Term* term = polynomial->head; term != NULL; term = term->next){
        int32_t next_degree = term->next == NULL ? 0 : term->next->degree;
        value = scale_at_point(value + term->coefficient, point, term->degree - next_degree);
        if (!isfinite(value))
            return STATUS_ERR_OVERFLOW;
    }
    *result = value;
    return STATUS_OK;
}

static const char* skip_space(const char* text){
    while (isspace((unsigned char)*text))
        text++;
    return text;
}

static char* trim(char* text){
    while (isspace((unsigned char)*text))
        text++;
    char* end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return text;
}

static int is_digit(char c){
    return c >= '0' && c <= '9';
}

static status_t read_digits(const char** text, int64_t limit, int64_t* result){
    if (text == NULL || *text == NULL || result == NULL)
        return STATUS_ERR_NULL;
    if (!is_digit(**text))
        return STATUS_ERR_INPUT;
    int64_t value = 0;
    while (is_digit(**text)){
        int digit = **text - '0';
        if (value > (limit - digit) / 10)
            return STATUS_ERR_OVERFLOW;
        value = value * 10 + digit;
        (*text)++;
    }
    *result = value;
    return STATUS_OK;
}

static status_t parse_term(const char** text, int sign, Polynomial* polynomial){
    if (text == NULL || *text == NULL || polynomial == NULL)
        return STATUS_ERR_NULL;
    const char* p = skip_space(*text);
    int64_t coefficient = 1, degree = 0;
    if (is_digit(*p)){
        int64_t limit = sign < 0 ? -(int64_t)INT32_MIN : INT32_MAX;
        status_t st = read_digits(&p, limit, &coefficient);
        if (st != STATUS_OK)
            return st;
        p = skip_space(p);
    } else if (*p != 'x'){
        return STATUS_ERR_INPUT;
    }
    if (*p == 'x'){
        degree = 1;
        p = skip_space(p + 1);
        if (*p == '^'){
            p = skip_space(p + 1);
            status_t st = read_digits(&p, INT32_MAX, &degree);
            if (st != STATUS_OK)
                return st;
        }
    }
    *text = skip_space(p);
    Term** link = &polynomial->head;
    while (*link != NULL && (*link)->degree > degree)
        link = &(*link)->next;
    return add_term(link, (int32_t)degree, sign * coefficient);
}

static status_t parse_polynomial(const char* text, Polynomial* result){
    if (text == NULL || result == NULL)
        return STATUS_ERR_NULL;
    Polynomial polynomial = {0};
    text = skip_space(text);
    status_t st;
    do {
        int sign = 1;
        if (*text == '+' || *text == '-'){
            sign = *text == '-' ? -1 : 1;
            text++;
        }
        st = parse_term(&text, sign, &polynomial);
    } while (st == STATUS_OK && (*text == '+' || *text == '-'));
    if (st == STATUS_OK && *text != '\0')
        st = STATUS_ERR_INPUT;
    if (st == STATUS_OK)
        *result = polynomial;
    else
        destroy_polynomial(&polynomial);
    return st;
}

static status_t parse_point(const char* text, long double* result){
    if (text == NULL || result == NULL)
        return STATUS_ERR_NULL;
    if (text[strspn(text, "0123456789.+-eE")] != '\0')
        return STATUS_ERR_INPUT;
    char* end;
    long double point = strtold(text, &end);
    if (end == text || *end != '\0')
        return STATUS_ERR_INPUT;
    if (!isfinite(point) || (fpclassify(point) == FP_ZERO &&
        strcspn(text, "123456789") < strcspn(text, "eE")))
        return STATUS_ERR_OVERFLOW;
    *result = point;
    return STATUS_OK;
}

static status_t parse_command(char* text, Command* result){
    if (text == NULL || result == NULL)
        return STATUS_ERR_NULL;
    char* first = strchr(text, '(');
    if (first == NULL)
        return STATUS_ERR_INPUT;
    *first++ = '\0';
    char* end = strchr(first, ')');
    if (end == NULL || *skip_space(end + 1) != '\0')
        return STATUS_ERR_INPUT;
    *end = '\0';

    const char* name = trim(text);
    const char* const names[] = {
        [OP_ADD] = "Add", [OP_SUB] = "Sub", [OP_MULT] = "Mult", [OP_DIV] = "Div",
        [OP_MOD] = "Mod", [OP_EVAL] = "Eval", [OP_DIFF] = "Diff", [OP_CMPS] = "Cmps"
    };
    size_t operation = 0;
    while (operation < sizeof(names) / sizeof(*names) && strcmp(name, names[operation]) != 0)
        operation++;
    if (operation == sizeof(names) / sizeof(*names))
        return STATUS_ERR_INPUT;

    char* second = strchr(first, ',');
    if (second != NULL){
        *second++ = '\0';
        if (strchr(second, ',') != NULL || operation == OP_DIFF)
            return STATUS_ERR_INPUT;
        second = trim(second);
    }
    first = trim(first);
    Command command = {.operation = (operation_t)operation};
    status_t st = STATUS_OK;
    if (command.operation == OP_DIFF){
        command.use_accumulator = *first == '\0';
        if (!command.use_accumulator)
            st = parse_polynomial(first, &command.first);
    } else {
        command.use_accumulator = second == NULL;
        if (!command.use_accumulator)
            st = parse_polynomial(first, &command.first);
        const char* last = command.use_accumulator ? first : second;
        if (st == STATUS_OK)
            st = command.operation == OP_EVAL ? parse_point(last, &command.point) :
                                                parse_polynomial(last, &command.second);
    }
    if (st == STATUS_OK)
        *result = command;
    else {
        destroy_polynomial(&command.first);
        destroy_polynomial(&command.second);
    }
    return st;
}

static status_t calculate_polynomial(const Command* command, const Polynomial* accumulator,
                                     Polynomial* result){
    if (command == NULL || accumulator == NULL || result == NULL)
        return STATUS_ERR_NULL;
    const Polynomial* first = command->use_accumulator ? accumulator : &command->first;
    const Polynomial* second = &command->second;
    Polynomial unused = {0};
    status_t st;
    switch (command->operation){
        case OP_ADD:  return add_polynomials(first, second, 0, result);
        case OP_SUB:  return add_polynomials(first, second, 1, result);
        case OP_MULT: return multiply_polynomials(first, second, result);
        case OP_DIFF: return differentiate_polynomial(first, result);
        case OP_CMPS: return compose_polynomials(first, second, result);
        case OP_DIV:
        case OP_MOD:
            st = command->operation == OP_DIV ? divide_polynomials(first, second, result, &unused) :
                                               divide_polynomials(first, second, &unused, result);
            destroy_polynomial(&unused);
            return st;
        case OP_EVAL: return STATUS_ERR_INPUT;
    }
    return STATUS_ERR_INPUT;
}

#endif
