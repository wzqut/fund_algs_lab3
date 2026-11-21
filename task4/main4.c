#include "functions4.h"

static status_t run_string_demo(void){
    String first = {0}, second = {0};
    String* copy = NULL;
    int order = 0, equal = 0;

    status_t st = string_create("box", &first);
    if (st == STATUS_OK)
        st = string_create("airmail", &second);
    if (st == STATUS_OK)
        st = string_compare(&first, &second, &order);
    if (st == STATUS_OK)
        st = string_equal(&first, &second, &equal);
    if (st == STATUS_OK && printf("[String demo]\n"
                                  "Created: \"%s\" (%d), \"%s\" (%d)\n"
                                  "Compare: %d; equal: %s\n",
                                  first.data, first.length, second.data, second.length,
                                  order, equal ? "yes" : "no") < 0)
        st = STATUS_ERR_WRITE;
    if (st == STATUS_OK)
        st = string_clone(&first, &copy);
    if (st == STATUS_OK)
        st = string_concat(&first, &second);
    if (st == STATUS_OK)
        st = string_copy(copy, &second);
    if (st == STATUS_OK)
        st = string_equal(copy, &second, &equal);
    if (st == STATUS_OK && printf("Concatenated: \"%s\"\n"
                                  "New copy: \"%s\"; existing copy: \"%s\"; equal: %s\n",
                                  first.data, copy->data, second.data, equal ? "yes" : "no") < 0)
        st = STATUS_ERR_WRITE;

    string_destroy(&first);
    if (st == STATUS_OK && printf("After destroy: length %d\n\n", first.length) < 0)
        st = STATUS_ERR_WRITE;
    string_destroy(&second);
    string_destroy(copy);
    free(copy);
    return st;
}

static status_t read_text(const char* prompt, String* result){
    if (fputs(prompt, stdout) == EOF || fflush(stdout) == EOF)
        return STATUS_ERR_WRITE;
    return read_line(stdin, result);
}

static status_t read_number(const char* prompt, unsigned int* result){
    String line = {0};
    status_t st = read_text(prompt, &line);
    if (st == STATUS_OK)
        st = parse_positive(&line, result);
    string_destroy(&line);
    return st;
}

static status_t read_address(const char* first_prompt, Address* a){
    status_t st = read_text(first_prompt, &a->city);
    if (st == STATUS_OK)
        st = read_text("Street: ", &a->street);
    if (st == STATUS_OK)
        st = read_number("House number: ", &a->house);
    if (st == STATUS_OK)
        st = read_text("Building (may be empty): ", &a->building);
    if (st == STATUS_OK)
        st = read_number("Apartment number: ", &a->apartment);
    if (st == STATUS_OK)
        st = read_text("Postal index (6 digits): ", &a->index);
    return st;
}

static status_t read_office(Address* a){
    for (;;){
        status_t st = read_address("[Post office address]\nCity: ", a);
        if (st == STATUS_OK)
            st = address_validate(a);
        if (st != STATUS_ERR_INPUT && st != STATUS_ERR_OVERFLOW)
            return st;
        fprintf(stderr, "Error: %s\n", status_message(st));
    }
}

static status_t action_add(Post* post){
    Mail mail = {0};
    String weight = {0};
    status_t st = read_address("[Recipient address]\nCity: ", &mail.recipient);
    if (st == STATUS_OK)
        st = read_text("Weight (non-negative, decimal point '.'): ", &weight);
    if (st == STATUS_OK)
        st = parse_weight(&weight, &mail.weight);
    if (st == STATUS_OK)
        st = read_text("Mail id (14 digits): ", &mail.id);
    if (st == STATUS_OK)
        st = read_text("Created (dd:MM:yyyy hh:mm:ss): ", &mail.created);
    if (st == STATUS_OK)
        st = read_text("Delivery deadline (dd:MM:yyyy hh:mm:ss): ", &mail.deadline);
    if (st == STATUS_OK)
        st = post_add(post, &mail);
    if (st == STATUS_OK && puts("Mail added") == EOF)
        st = STATUS_ERR_WRITE;
    string_destroy(&weight);
    mail_destroy(&mail);
    return st;
}

static status_t action_by_id(Post* post, char choice){
    String id = {0};
    status_t st = read_text("Mail id (14 digits): ", &id);
    if (st != STATUS_OK)
        return st;

    if (choice == '3'){
        size_t pos;
        st = post_find(post, &id, &pos);
        if (st == STATUS_OK)
            st = write_mail(stdout, &post->items[pos]);
    } else if (choice == '2' || choice == '4'){
        int remove_mail = choice == '2';
        st = remove_mail ? post_remove(post, &id) : post_deliver(post, &id);
        const char* message = remove_mail ? "Mail removed" : "Mail marked as delivered";
        if (st == STATUS_OK && puts(message) == EOF)
            st = STATUS_ERR_WRITE;
    } else {
        st = STATUS_ERR_INPUT;
    }
    string_destroy(&id);
    return st;
}

static status_t action_list(const Post* post, filter_t filter){
    const Mail** matches = NULL;
    size_t count = 0;
    status_t st = post_filter(post, filter, &matches, &count);
    if (st == STATUS_OK && printf("%s mail: %zu\n", filter == FILTER_DELIVERED ? "Delivered" : "Overdue", count) < 0)
        st = STATUS_ERR_WRITE;
    for (size_t i = 0; i < count && st == STATUS_OK; i++)
        st = write_mail(stdout, matches[i]);
    free(matches);
    return st;
}

static status_t run_dialog(Post* post){
    const char* menu = "\n[Mail]\n"
                       "1 - add mail\n2 - remove mail\n3 - find mail by id\n"
                       "4 - mark mail as delivered\n5 - list delivered mail\n"
                       "6 - list overdue mail\n0 - exit\nYour choice: ";

    const Address* address = post->address;
    if (printf("Post office: %s, %s, index %s\n",
               address->city.data, address->street.data, address->index.data) < 0)
        return STATUS_ERR_WRITE;

    for (;;){
        String line = {0};
        status_t st = read_text(menu, &line);
        char choice = line.length == 1 ? line.data[0] : '\0';
        string_destroy(&line);
        if (st == STATUS_OK){
            switch (choice){
                case '0': return STATUS_OK;
                case '1': st = action_add(post); break;
                case '2':
                case '3':
                case '4': st = action_by_id(post, choice); break;
                case '5': st = action_list(post, FILTER_DELIVERED); break;
                case '6': st = action_list(post, FILTER_EXPIRED); break;
                default:  st = STATUS_ERR_INPUT; break;
            }
        }

        if (st == STATUS_ERR_EOF || st == STATUS_ERR_READ || st == STATUS_ERR_WRITE ||
            st == STATUS_ERR_ALLOC || st == STATUS_ERR_NULL)
            return st;
        if (st != STATUS_OK)
            fprintf(stderr, "Error: %s\n", status_message(st));
    }
}

int main(void){
    Address office = {0};
    Post post = {&office, NULL, 0, 0};
    status_t st = run_string_demo();
    if (st == STATUS_OK)
        st = read_office(&office);
    if (st == STATUS_OK)
        st = run_dialog(&post);
    if (st == STATUS_ERR_EOF)
        st = STATUS_OK;
    if (fflush(stdout) == EOF && st == STATUS_OK)
        st = STATUS_ERR_WRITE;
    if (st != STATUS_OK)
        fprintf(stderr, "Error: %s\n", status_message(st));

    post_destroy(&post);
    address_destroy(&office);
    return st == STATUS_OK ? 0 : 1;
}
