#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <iso646.h>
#include <stdlib.h>
#include <memory.h>
#include "reflect.h"


$if_clang (
    _Pragma("clang assume_nonnull begin")
)


static uint64_t hash_data(size_t size, uint8_t data[static size])
{
    uint64_t hash = 0;
    for (size_t i = 0; i < size; i++) {
        hash = data[i] + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

static struct Buffer *$nullable buffer_create(size_t size)
{
    struct Buffer *buffer = calloc(1, sizeof(struct Buffer) + size);
    if (buffer == nullptr)
        return nullptr;

    buffer->size = size;
    return buffer;
}

static struct Buffer *$nullable buffer_append(struct Buffer *buffer, size_t size, uint8_t data[static size])
{
    void *newbuf = realloc(buffer, buffer->size + size);
    if (newbuf == nullptr)
        return nullptr;
    buffer = newbuf;
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return buffer;
}


static _Atomic(struct {
    char *name;
    struct Type type;
} *) type_cache = nullptr;
static _Atomic(size_t) type_cache_count = 0;

static int clone_type(struct Type *dest, const struct Type *src) {
    memset(dest, 0, sizeof(struct Type));
    dest->type = src->type;
    dest->modifiers = src->modifiers;
    dest->hash = src->hash;

    switch (src->type) {
        case TypeType_PRIMITIVE:
            dest->primitive = src->primitive;
            break;

        case TypeType_POINTER:
             dest->pointer.type = malloc(sizeof(struct Type));
             if (dest->pointer.type == nullptr) return 1;
             if (clone_type(dest->pointer.type, src->pointer.type) != 0) return 1;
             break;

        case TypeType_ARRAY:
             dest->array.length = src->array.length;
             dest->array.type = malloc(sizeof(struct Type));
             if (dest->array.type == nullptr) return 1;
             if (clone_type(dest->array.type, src->array.type) != 0) return 1;
             break;

        case TypeType_STRUCT:
             dest->structure.name = buffer_create(src->structure.name->size + 1);
             if (dest->structure.name == nullptr) return 1;
             dest->structure.name->size = src->structure.name->size;
             memcpy(dest->structure.name->data, src->structure.name->data, src->structure.name->size);

             if (src->structure.field_count > 0) {
                 dest->structure.field_count = src->structure.field_count;
                 dest->structure.fields = calloc(src->structure.field_count, sizeof(struct Field));
                 if (dest->structure.fields == nullptr) return 1;

                 for (size_t i=0; i < src->structure.field_count; i++) {
                     struct Field *df = &dest->structure.fields[i];
                     const struct Field *sf = &src->structure.fields[i];

                     df->name = buffer_create(sf->name->size + 1); // check null
                     if (df->name == nullptr) return 1;
                     df->name->size = sf->name->size;
                     memcpy(df->name->data, sf->name->data, sf->name->size);

                     df->offset = sf->offset;

                     if (sf->length_field_name) {
                         df->length_field_name = buffer_create(sf->length_field_name->size + 1);
                         if (df->length_field_name == nullptr) return 1;
                         df->length_field_name->size = sf->length_field_name->size;
                         memcpy(df->length_field_name->data, sf->length_field_name->data, sf->length_field_name->size);
                     }

                     if (sf->modifier_count > 0) {
                         df->modifier_count = sf->modifier_count;
                         df->modifiers = calloc(sf->modifier_count, sizeof(struct Buffer *));
                         if (df->modifiers == nullptr) return 1;
                         for (size_t m=0; m < sf->modifier_count; m++) {
                             df->modifiers[m] = buffer_create(sf->modifiers[m]->size + 1);
                             if (df->modifiers[m] == nullptr) return 1;
                             df->modifiers[m]->size = sf->modifiers[m]->size;
                             memcpy(df->modifiers[m]->data, sf->modifiers[m]->data, sf->modifiers[m]->size);
                         }
                     }

                     if (clone_type(&df->type, &sf->type) != 0) return 1;
                 }
             }
             break;
    }
    return 0;
}

static void cache_type_definition(struct Type *type) {
    if (type->type != TypeType_STRUCT || type->structure.field_count == 0) return;

    // Check if exists
    // The name in the buffer is not null terminated, but buffer_create adds + size?
    // Wait, Buffer struct: `uint8_t data[]`. memory.h says calloc(1, size + ...).
    // so data is raw bytes.
    // We need to match by string.

    char *name = malloc(type->structure.name->size + 1);
    if (!name) return;
    memcpy(name, type->structure.name->data, type->structure.name->size);
    name[type->structure.name->size] = 0;

    for (size_t i = 0; i < type_cache_count; i++) {
        if (strcmp(type_cache[i].name, name) == 0) {
            free(name);
            return; // Already cached
        }
    }

    // Add to cache
    type_cache = realloc(type_cache, (type_cache_count + 1) * sizeof(*type_cache));
    if (!type_cache) { free(name); return; }

    type_cache[type_cache_count].name = name;
    if (clone_type(&type_cache[type_cache_count].type, type) != 0) {
        free(name);
        return;
    }

    type_cache_count++;
}

static bool lookup_cached_type(struct Type *type, const char *name_str, size_t name_len) {
    char *name = malloc(name_len + 1);
    memcpy(name, name_str, name_len);
    name[name_len] = 0;

    for (size_t i = 0; i < type_cache_count; i++) {
        if (strcmp(type_cache[i].name, name) == 0) {
             // Found! Clone it
             struct Type *cached = &type_cache[i].type;
             uint32_t mods = type->modifiers;
             free(type->structure.name);

             if (clone_type(type, cached) != 0) {
                 free(name);
                 return false;
             }

             type->modifiers |= mods;

             free(name);
             return true;
        }
    }
    free(name);
    return false;
}

static uint64_t hash_type(struct Type type)
{
    uint64_t hash = 0;
    switch (type.type) {
        case TypeType_PRIMITIVE:
            hash = type.primitive.type;
            break;

        case TypeType_STRUCT:
            hash = hash_data(type.structure.name->size, type.structure.name->data);
            for (size_t i = 0; i < type.structure.field_count; i++) {
                hash += hash_type(type.structure.fields[i].type);
            }
            break;

        case TypeType_ARRAY:
            hash = type.array.length * hash_type(*type.array.type);
            break;

        case TypeType_POINTER:
            hash = hash_type(*type.pointer.type);
            hash *= 31;
            hash += TypeEncoding_POINTER;
            break;
    }

    hash += type.modifiers;

    return hash;
}

// {Point=(?={$x$=}i)(?={$y$=}i)}
int parse_type(struct Type *type, const char **str)
{
    while (**str == TypeModifierEncoding_CONST) { // 'r'
        type->modifiers |= TypeModifiers_CONST;
        (*str)++;
    }

    char encoding = **str;
    switch (encoding) {
        //already handled, we have to do it before because GCC and Clang differ with const placement
        case TypeModifierEncoding_CONST:
            break;

        case TypeEncoding_POINTER: {
             type->type = TypeType_POINTER;
            (*str)++;

            // GCC encodes `const int *` as `^ri` (Pointer to Const Int).
            // Clang encodes `const int *` as `r^i` (Const Pointer to Int).
            // Yes, this is incredibly stupid.

            void *p = type->pointer.type = malloc(sizeof(struct Type));
            if (p == nullptr)
                return 1;
            if (parse_type(type->pointer.type, str) != 0)
                return 1;
            break;
        }
        case TypeEncoding_UNSIGNED_CHAR:
        case TypeEncoding_SIGNED_SHORT:
        case TypeEncoding_UNSIGNED_SHORT:
        case TypeEncoding_SIGNED_INT:
        case TypeEncoding_UNSIGNED_INT:
        case TypeEncoding_SIGNED_LONG:
        case TypeEncoding_UNSIGNED_LONG:
        case TypeEncoding_SIGNED_LONG_LONG:
        case TypeEncoding_UNSIGNED_LONG_LONG:
        case TypeEncoding_FLOAT:
        case TypeEncoding_DOUBLE:
        case TypeEncoding_LONG_DOUBLE:
        case TypeEncoding_CHAR_POINTER:
        case TypeEncoding_VOID:
        case TypeEncoding_BOOL: {
            type->type = TypeType_PRIMITIVE;
            type->primitive.type = encoding;
            (*str)++;
            break;
        }

        case TypeEncoding_STRUCT: {
            type->type = TypeType_STRUCT;
            (*str)++;

            char *$nullable endptr = strpbrk(*str, "=}");
            if (endptr == nullptr) {
                return 1;
            }
            if (*endptr == '}') {
                //likley there is just a name here
                struct Buffer *$nullable buf = buffer_create(endptr - *str + 1);
                if (buf == nullptr)
                    return 1;
                memcpy(buf->data, *str, endptr - *str);
                buf->size = endptr - *str;

                type->structure.name = $nonnull_cast(buf);
                type->structure.field_count = 0;
                type->structure.fields = nullptr;

                // Try to resolve from cache
                if (lookup_cached_type(type, (char*)buf->data, buf->size)) {
                    *str = $nonnull_cast(endptr) + 1;
                    break;
                }

                *str = $nonnull_cast(endptr) + 1;
                break;
            }
            struct Buffer *$nullable buf = buffer_create(endptr - *str + 1);
            if (buf == nullptr)
                return 1;
            memcpy(buf->data, *str, endptr - *str);
            buf->size = endptr - *str;

            type->structure.name = $nonnull_cast(buf);
            *str = $nonnull_cast(endptr) + 1;

            // (?=i{$x$6=})(?=f{$y$7=})

            type->structure.field_count = 0;
            char curchar;
            size_t offset = 0;
            while ((curchar = *(*str)++) != '}') {
                type->structure.field_count++;
                void *$nullable newfields = realloc(type->structure.fields, type->structure.field_count * sizeof(struct Field));
                if (newfields == nullptr)
                    return 1;
                type->structure.fields = $nonnull_cast(newfields);

                struct Field *field = &type->structure.fields[type->structure.field_count - 1];
                *field = (struct Field){0};

                //(?=i{$x$6=}), parse type first, then field name
                if (curchar == '(' and **str == '?' and (*str)[1] == '=') { //this is a struct field decl
                    *str = *str + 2;

                    // Parse the type first (e.g., 'i' for int)
                    if (parse_type(&field->type, str) != 0)
                        return 1;

                    // Now expect '{'
                    if (**str != '{')
                        return 2;
                    (*str)++;

                    // Skip the '$'
                    if (**str != '$')
                        return 2;
                    (*str)++;

                    // Extract field name until next '$'
                    endptr = strchr(*str, '$');
                    if (endptr == nullptr)
                        return 1;

                    struct Buffer *$nullable field_name = buffer_create(endptr - *str + 1);
                    if (field_name == nullptr)
                        return 1;
                    memcpy(field_name->data, *str, endptr - *str);
                    field_name->size = endptr - *str;
                    *str = endptr + 1;

                    //next is the line number, which we don't care about (used to make a unique tag)
                    strtol(*str, &endptr, 10);
                    *str = endptr;

                    // Parse modifiers (if any after line number)
                    field->modifier_count = 0;
                    field->modifiers = nullptr;
                    field->length_field_name = nullptr;

                    while (**str == '$') {
                        (*str)++;
                        char *sep = strpbrk(*str, "$}=");
                        if (sep == nullptr) {
                            break;
                        }

                        size_t mod_len = sep - *str;
                        struct Buffer *mod = buffer_create(mod_len + 1);
                        memcpy(mod->data, *str, mod_len);
                        mod->size = mod_len;

                        // Check if this is a sized_by modifier
                        if (strncmp(*str, "sized_by_", 9) == 0) {
                            size_t len_offset = 9;
                            size_t len_name_len = sep - (*str + len_offset);
                            struct Buffer *$nullable len_name = buffer_create(len_name_len + 1);
                            if (len_name == nullptr)
                                return 1;
                            memcpy(len_name->data, *str + len_offset, len_name_len);
                            len_name->size = len_name_len;
                            field->length_field_name = $nonnull_cast(len_name);
                        }

                        // Add to modifiers array regardless
                        field->modifier_count++;
                        field->modifiers = realloc(field->modifiers, field->modifier_count * sizeof(struct Buffer *));
                        field->modifiers[field->modifier_count - 1] = mod;

                        *str = sep;
                    }

                    // Skip `=` and `}`
                    while (**str == '=' || **str == '}') (*str)++;

                    field->name = $nonnull_cast(field_name);

                    field->offset = offset;
                    offset += type_size(field->type);

                    //skip the `)`
                    if (**str != ')') {
                        return 2;
                    }
                    (*str)++;
                } else {
                    fprintf(stderr, "Unexpected character %c. Did you forget to make it a $field(...)?\n", curchar);
                    return 2;
                }
            }

            cache_type_definition(type);
            break;
        }

        case TypeEncoding_ARRAY: {
            type->type = TypeType_ARRAY;
            (*str)++;
            char *endptr;
            size_t len = strtol(*str, &endptr, 10);
            if (endptr == *str)
                return 1;
            type->array.length = len;
            *str = endptr;

            void *p = type->array.type = malloc(sizeof(struct Type));
            if (p == nullptr)
                return 1;
            if (parse_type(type->array.type, str) != 0)
                return 1;

            //skip the `]`
            if (**str != ']') {
                return 2;
            }
            (*str)++;

            break;
        }

    }

    type->hash = hash_type(*type);

    return 0;
}

void free_type(struct Type *type)
{
    switch (type->type) {
        case TypeType_PRIMITIVE:
            break;

        case TypeType_STRUCT:
            free(type->structure.name);
            for (size_t i = 0; i < type->structure.field_count; i++) {
                free_type(&type->structure.fields[i].type);
                free(type->structure.fields[i].name);
                 if (type->structure.fields[i].length_field_name) free(type->structure.fields[i].length_field_name);
                for (size_t j = 0; j < type->structure.fields[i].modifier_count; j++) {
                    free(type->structure.fields[i].modifiers[j]);
                }
                free(type->structure.fields[i].modifiers);
            }
            free(type->structure.fields);
            break;

        case TypeType_ARRAY:
            free_type(type->array.type);
            free(type->array.type);
            break;

        case TypeType_POINTER:
            free_type(type->pointer.type);
            free(type->pointer.type);
            break;
    }

    *type = (struct Type){0};
}


struct Value get_value(struct Value value, const char *field)
{
    assert(value.type.type == TypeType_STRUCT);

    for (size_t i = 0; i < value.type.structure.field_count; i++) {
        if (strcmp((const char *)value.type.structure.fields[i].name->data, field) == 0) {
            return (struct Value) {
                .type = value.type.structure.fields[i].type,
                .data = value.data + value.type.structure.fields[i].offset
            };
        }
    }

    return (struct Value){0};
}

int value_to_json(FILE *buf, struct Value val)
{
    // fprintf(buf, "{");
    {
        switch (val.type.type) {
            case TypeType_PRIMITIVE: {
                switch (val.type.primitive.type) {
                    case TypeEncoding_SIGNED_CHAR:
                    case TypeEncoding_UNSIGNED_CHAR:
                        fprintf(buf, "%d", (int)*(char *)val.data);
                        break;

                    case TypeEncoding_SIGNED_SHORT:
                    case TypeEncoding_UNSIGNED_SHORT:
                        fprintf(buf, "%d", (int)*(short *)val.data);
                        break;

                    case TypeEncoding_SIGNED_INT:
                    case TypeEncoding_UNSIGNED_INT:
                        fprintf(buf, "%d", *(int *)val.data);
                        break;

                    case TypeEncoding_SIGNED_LONG:
                    case TypeEncoding_UNSIGNED_LONG:
                        fprintf(buf, "%ld", *(long *)val.data);
                        break;

                    case TypeEncoding_SIGNED_LONG_LONG:
                    case TypeEncoding_UNSIGNED_LONG_LONG:
                        fprintf(buf, "%lld", *(long long *)val.data);
                        break;

                    case TypeEncoding_FLOAT:
                        fprintf(buf, "%f", *(float *)val.data);
                        break;

                    case TypeEncoding_DOUBLE:
                        fprintf(buf, "%f", *(double *)val.data);
                        break;

                    case TypeEncoding_LONG_DOUBLE:
                        fprintf(buf, "%Lf", *(long double *)val.data);
                        break;

                    case TypeEncoding_CHAR_POINTER:
                        fprintf(buf, "\"%s\"", *(char **)val.data);
                        break;

                    case TypeEncoding_POINTER:
                        fprintf(buf, "%p", *(void **)val.data);
                        break;

                    case TypeEncoding_BOOL:
                        fprintf(buf, "%s", *(bool *)val.data ? "true" : "false");
                        break;
                }
                break;
            }

            case TypeType_STRUCT: {
                fprintf(buf, "{");
                for (size_t i = 0; i < val.type.structure.field_count; i++) {
                    struct Field *field = &val.type.structure.fields[i];
                    if (field_has_modifier(*field, "no_serialise")) {
                        continue;
                    }
                    const char *json_key = (const char *)field->name->data;
                    for (size_t k = 0; k < field->modifier_count; k++) {
                        const char *mod = (const char *)field->modifiers[k]->data;
                        if (strncmp(mod, "serialise_as_", 13) == 0) {
                            json_key = mod + 13;
                            break;
                        }
                    }

                    fprintf(buf, "\"%s\": ", json_key);

                    bool handled = false;

                    const char *target_length_field = nullptr;
                    if (field->length_field_name != nullptr) {
                        target_length_field = (const char *)field->length_field_name->data;
                    } else {
                         for (size_t k = 0; k < field->modifier_count; k++) {
                             const char *mod = (const char *)field->modifiers[k]->data;
                             if (strncmp(mod, "sized_by_", 9) == 0) {
                                 target_length_field = mod + 9;
                                 break;
                             }
                             if (strcmp(mod, "sizedby") == 0) {
                                 if (k + 1 < field->modifier_count) {
                                     target_length_field = (const char *)field->modifiers[k + 1]->data;
                                 }
                                 break;
                             }
                         }
                    }

                    if (target_length_field != nullptr) {
                        struct Value len_val = get_value(val, target_length_field);
                        if (len_val.data != nullptr && len_val.type.type == TypeType_PRIMITIVE) {
                            size_t len = 0;
                            bool ok = true;
                            switch (len_val.type.primitive.type) {
                                case TypeEncoding_SIGNED_CHAR: len = *(signed char *)len_val.data; break;
                                case TypeEncoding_UNSIGNED_CHAR: len = *(unsigned char *)len_val.data; break;
                                case TypeEncoding_SIGNED_SHORT: len = *(short *)len_val.data; break;
                                case TypeEncoding_UNSIGNED_SHORT: len = *(unsigned short *)len_val.data; break;
                                case TypeEncoding_SIGNED_INT: len = *(int *)len_val.data; break;
                                case TypeEncoding_UNSIGNED_INT: len = *(unsigned int *)len_val.data; break;
                                case TypeEncoding_SIGNED_LONG: len = *(long *)len_val.data; break;
                                case TypeEncoding_UNSIGNED_LONG: len = *(unsigned long *)len_val.data; break;
                                case TypeEncoding_SIGNED_LONG_LONG: len = *(long long *)len_val.data; break;
                                case TypeEncoding_UNSIGNED_LONG_LONG: len = *(unsigned long long *)len_val.data; break;
                                default: ok = false; break;
                            }

                            if (ok) {
                                if (field->type.type == TypeType_PRIMITIVE && field->type.primitive.type == TypeEncoding_CHAR_POINTER) {
                                    char *ptr = *(char **)(val.data + field->offset);
                                    if (ptr == nullptr) {
                                        fprintf(buf, "null");
                                    } else {
                                        fprintf(buf, "\"");
                                        for (size_t k = 0; k < len; k++) {
                                            unsigned char c = ptr[k];
                                            if (c == '"' || c == '\\') fprintf(buf, "\\%c", c);
                                            else if (c == '\n') fprintf(buf, "\\n");
                                            else if (c == '\r') fprintf(buf, "\\r");
                                            else if (c == '\t') fprintf(buf, "\\t");
                                            else if (c < 32 || c > 126) fprintf(buf, "\\u%04x", c);
                                            else fputc(c, buf);
                                        }
                                        fprintf(buf, "\"");
                                    }
                                    handled = true;
                                } else if (field->type.type == TypeType_POINTER) {
                                    void *ptr = *(void **)(val.data + field->offset);
                                    if (ptr == nullptr) {
                                        fprintf(buf, "null");
                                    } else {
                                        fprintf(buf, "[");
                                        struct Type elem_type = *field->type.pointer.type;
                                        size_t elem_size = type_size(elem_type);
                                        for (size_t k = 0; k < len; k++) {
                                            if (value_to_json(buf, (struct Value) {
                                                .type = elem_type,
                                                .data = (char *)ptr + (k * elem_size)
                                            }) != 0) return 1;
                                            if (k != len - 1) fprintf(buf, ", ");
                                        }
                                        fprintf(buf, "]");
                                    }
                                    handled = true;
                                }
                            }
                        }
                    }

                    if (!handled) {
                        if (value_to_json(buf, (struct Value) {
                            .type = field->type,
                            .data = val.data + field->offset
                        }) != 0)
                            return 1;
                    }

                    if (i != val.type.structure.field_count - 1) {
                        fprintf(buf, ", ");
                    }
                }
                fprintf(buf, "}");
                break;
            }

            case TypeType_ARRAY: {
                fprintf(buf, "[");
                for (size_t i = 0; i < val.type.array.length; i++) {
                    if (value_to_json(buf, (struct Value){
                        .type = *val.type.array.type,
                        .data = val.data + (i * type_size(*val.type.array.type))
                    }) != 0)
                        return 1;
                    if (i != val.type.array.length - 1)
                        fprintf(buf, ", ");
                }
                fprintf(buf, "]");
                break;
            }

            case TypeType_POINTER: {
                if (*(void **)val.data == nullptr) {
                    fprintf(buf, "null");
                } else {
                    if (value_to_json( buf, (struct Value) {
                        .type = *val.type.pointer.type,
                        .data = *(void **)val.data
                    }) != 0)
                        return 1;
                }
                break;
            }
        }
    }
    // fprintf(buf, "}");

    return 0;
}

void print_type(FILE *to, struct Type type, int indent)
{
    size_t tysiz = type_size(type);
    if (type.modifiers & TypeModifiers_CONST) {
        fprintf(to, "const ");
    }
    switch (type.type) {
        case TypeType_PRIMITIVE:
            fprintf(to, "%c\n", type.primitive.type);
            break;

        case TypeType_STRUCT:
            fprintf(to, "%s (size: %zu) ", type.structure.name->data, tysiz);
            if (type.structure.field_count > 0) {
                fprintf(to, " {\n");
                for (size_t i = 0; i < type.structure.field_count; i++) {
                    struct Field *field = &type.structure.fields[i];
                    fprintf(to, "%*s(+%zu) %s: ", indent + 4, "", field->offset, field->name->data);
                    print_type(to, field->type, indent + 4);
                }
                fprintf(to, "%*s}", indent, "");
            }
            fprintf(to, "\n");
            break;

        case TypeType_ARRAY:
            fprintf(to, "[%zu]", type.array.length);
            print_type(to, *type.array.type, indent);
            break;

        case TypeType_POINTER:
            fprintf(to, "*");
            print_type(to, *type.pointer.type, indent + 4);
            break;
    }
}

const char *type_name(struct Type type)
{
    switch (type.type) {
        case TypeType_PRIMITIVE: {
            switch (type.primitive.type) {
                case TypeEncoding_SIGNED_CHAR:
                    return "signed char";

                case TypeEncoding_UNSIGNED_CHAR:
                    return "unsigned char";

                case TypeEncoding_SIGNED_SHORT:
                    return "signed short";

                case TypeEncoding_UNSIGNED_SHORT:
                    return "unsigned short";

                case TypeEncoding_SIGNED_INT:
                    return "signed int";

                case TypeEncoding_UNSIGNED_INT:
                    return "unsigned int";

                case TypeEncoding_SIGNED_LONG:
                    return "signed long";

                case TypeEncoding_UNSIGNED_LONG:
                    return "unsigned long";

                case TypeEncoding_SIGNED_LONG_LONG:
                    return "signed long long";

                case TypeEncoding_UNSIGNED_LONG_LONG:
                    return "unsigned long long";

                case TypeEncoding_FLOAT:
                    return "float";

                case TypeEncoding_DOUBLE:
                    return "double";

                case TypeEncoding_LONG_DOUBLE:
                    return "long double";

                case TypeEncoding_CHAR_POINTER:
                    return "char *";

                case TypeEncoding_VOID:
                    return "void";

                case TypeEncoding_BOOL:
                    return "_Bool";

                default:
                    return "unknown";
            }
            break;
        }

        case TypeType_STRUCT:
            return (const char *)type.structure.name->data;

        case TypeType_ARRAY:
            return "array";

        case TypeType_POINTER:
            return "pointer";

        default:
            return "unknown";
    }
}

bool field_has_modifier(struct Field field, const char *modifier)
{
    for (size_t i = 0; i < field.modifier_count; i++) {
        if (strcmp((const char *)field.modifiers[i]->data, modifier) == 0) {
            return true;
        }
    }
    return false;
}

$if_clang (
    _Pragma("clang assume_nonnull end")
)
