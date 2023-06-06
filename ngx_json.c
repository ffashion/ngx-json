#include "ngx_json.h"
#include "ngx_buf.h"
#include "ngx_config.h"
#include "ngx_palloc.h"
#include "ngx_string.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>


static ngx_int_t json_print_type_value(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth);
static ngx_int_t json_print_type_string(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth);
static ngx_int_t json_print_type_array(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth);
static ngx_int_t json_print_type_object(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth);
static ngx_int_t json_print_type_number(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth);
static ngx_int_t json_print_string(ngx_pool_t *pool, bool format, ngx_str_t *string, ngx_buf_t *buf, ngx_int_t *depth);

#define ngx_buf_capacity(b) b->end - b->start

static ngx_int_t ngx_buf_append_string(ngx_pool_t *pool, ngx_buf_t *b, u_char *s, ngx_int_t len) {
    u_char     *p;
	ngx_uint_t capacity, size;

	if (len > (ngx_int_t)(b->end - b->last)) {

		size = ngx_buf_size(b);

		capacity = ngx_buf_capacity(b);
		capacity <<= 1;

		if (capacity < (size + len)) {
			capacity = size + len;
		}

		p = ngx_palloc(pool, capacity);
		if (p == NULL) {
			return NGX_ERROR;
		}

		b->last = ngx_copy(p, b->pos, size);

		b->start = b->pos = p;
		b->end = p + capacity;
	}

	b->last = ngx_copy(b->last, s, len);

	return NGX_OK;
}

static ngx_int_t ngx_buf_append_str(ngx_pool_t *pool, ngx_buf_t *b, ngx_str_t *str) {
    return ngx_buf_append_string(pool, b, str->data, str->len);
}

static ngx_int_t ngx_buf_append_char(ngx_pool_t *pool, ngx_buf_t *b, u_char c) {
    ngx_str_t str;
    str.len = 1;
    str.data = &c;
    return ngx_buf_append_string(pool, b, str.data, str.len);
}

static ngx_int_t ngx_buf_to_str(ngx_pool_t *pool, ngx_buf_t *buf, ngx_str_t *v) {
    v->data = buf->pos;
    v->len = ngx_buf_size(buf);
    return NGX_OK;
}

static bool compare_double(double a, double b) {
    double max = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return (fabs(a - b) <= max * DBL_EPSILON);
}

ngx_json_t *ngx_json_new(ngx_pool_t *pool, ngx_int_t type) {
    ngx_json_t *node;

    node = ngx_pcalloc(pool, sizeof(ngx_json_t));
    if (node == NULL) {
        return NULL;
    }

    node->type = type;

    return node;
}

ngx_json_t *ngx_json_new_null(ngx_pool_t *pool) {
    ngx_json_t *node;
    node = ngx_json_new(pool, NGX_JSON_NULL);
    if (node == NULL) {
        return NULL;
    }
    return node;
}

ngx_json_t *ngx_json_new_number(ngx_pool_t *pool, double num) {
    ngx_json_t *node;

    node = ngx_json_new(pool, NGX_JSON_NUMBER);
    if (node == NULL) {
        return NULL;
    }

    node->valuedouble = num;
    
    //default value
    node->valueint = (int)num;

    if (num >= INT_MAX) {
        node->valueint = INT_MAX;
    }

    if (num <= INT_MIN) {
        node->valueint = INT_MIN;
    }

    return node;
}

ngx_json_t *ngx_json_new_string(ngx_pool_t *pool, ngx_str_t *value) {
    ngx_json_t *node;

    node = ngx_json_new(pool, NGX_JSON_STRING);
    if (node == NULL) {
        return NULL;
    }

    node->valuestring = *value;

    return node;
}

ngx_json_t *ngx_json_new_obj(ngx_pool_t *pool) {
    ngx_json_t *node;

    node = ngx_json_new(pool, NGX_JSON_OBJECT);
    if (node == NULL) {
        return NULL;
    }

    ngx_queue_init(&node->list);
    return node;
}


ngx_json_t *ngx_json_new_array(ngx_pool_t *pool) {
    ngx_json_t *node;

    node = ngx_json_new(pool, NGX_JSON_ARRAY);
    if (node == NULL) {
        return NULL;
    }

    ngx_queue_init(&node->list);

    return node;
}

static ngx_int_t ngx_json_add_item(ngx_pool_t *pool, ngx_json_t *json, ngx_json_t *item) {
    if (json == NULL || item == NULL) {
        return NGX_OK;
    }

    ngx_queue_insert_tail(&json->list, &item->list);
    return NGX_OK;
}

ngx_int_t ngx_json_add_item_to_array(ngx_pool_t *pool, ngx_json_t *array, ngx_json_t *item) {
    assert(array->type == NGX_JSON_ARRAY);
    return ngx_json_add_item(pool, array, item);
}

ngx_int_t ngx_json_add_item_to_object(ngx_pool_t *pool, ngx_json_t *obj, ngx_str_t *key, ngx_json_t *value) {
    assert(obj->type == NGX_JSON_OBJECT);
    if (obj == NULL || key == NULL || value == NULL) {
        return NGX_OK;
    }
    
    value->key = *key;

    return ngx_json_add_item(pool, obj, value);
}

static ngx_int_t json_print_type_object(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth) {
    ngx_queue_t        *q;
    ngx_json_t         *node;
    ngx_int_t i;

    (*depth)++;
    if (ngx_buf_append_char(pool, buf, '{') != NGX_OK) {
        return NGX_ERROR;
    }

    if (format) {
        if (ngx_buf_append_char(pool, buf, '\n') != NGX_OK) {
            return NGX_ERROR;
        }
    }

    for (q = ngx_queue_head(&item->list);
    q != ngx_queue_sentinel(&item->list);
    q = ngx_queue_next(q)) {
        node = ngx_queue_data(q, ngx_json_t, list);
        if (format) {
            for (i = 0; i < *depth; i++) {
                if (ngx_buf_append_char(pool, buf, '\t') != NGX_OK) {
                    return NGX_ERROR;
                }
            }
        }

        if (json_print_string(pool, format, &node->key, buf, depth) != NGX_OK) {
            return NGX_ERROR;
        }

        if (ngx_buf_append_char(pool, buf, ':') != NGX_OK) {
            return NGX_ERROR;
        }

        if (format) {
            if (ngx_buf_append_char(pool, buf, '\t') != NGX_OK) {
                return NGX_ERROR;
            }
        }

        if (json_print_type_value(pool, format, node, buf, depth) != NGX_OK) {
            return NGX_ERROR;
        }

        if (ngx_queue_next(q) != ngx_queue_sentinel(&item->list)) {
            if (ngx_buf_append_char(pool, buf, ',') != NGX_OK) {
                return NGX_ERROR;
            }
        }

        if (format) {
            if (ngx_buf_append_char(pool, buf, '\n') != NGX_OK) {
                return NGX_ERROR;
            }
        }
    }

    if (format) {
        for (i = 0; i < ((*depth) -1); i++) {
            if (ngx_buf_append_char(pool, buf, '\t') != NGX_OK) {
                return NGX_ERROR;
            }
        }
    }

    if (ngx_buf_append_char(pool, buf, '}') != NGX_OK) {
        return NGX_ERROR;
    }

    (*depth)--;
    return NGX_OK;
}

static ngx_int_t json_print_type_array(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth) {
    ngx_queue_t        *q;
    ngx_json_t         *node;

    (*depth)++;
    if (ngx_buf_append_char(pool, buf, '[') != NGX_OK) {
        return NGX_ERROR;
    }

    for (q = ngx_queue_head(&item->list);
    q != ngx_queue_sentinel(&item->list);
    q = ngx_queue_next(q)) {
        node = ngx_queue_data(q, ngx_json_t, list);

        if (json_print_type_value(pool, format, node, buf, depth) != NGX_OK) {
            return NGX_ERROR;
        }

        if (ngx_queue_next(q) != ngx_queue_sentinel(&item->list)) {
            if (ngx_buf_append_char(pool, buf, ',') != NGX_OK) {
                return NGX_ERROR;
            }

            if (format) {
                if (ngx_buf_append_char(pool, buf, ' ') != NGX_OK) {
                    return NGX_ERROR;
                }
            }
        }
    }

    if (ngx_buf_append_char(pool, buf, ']') != NGX_OK) {
        return NGX_ERROR;
    }

    (*depth)--;

    return NGX_OK;
}

static ngx_int_t json_print_string(ngx_pool_t *pool, bool format, ngx_str_t *string, ngx_buf_t *buf, ngx_int_t *depth) {
    ngx_str_t v;
    char c;
    ngx_uint_t i;
    char buffer[5];

    if (string->len == 0) {
        ngx_str_set(&v, "\"\"");
        if (ngx_buf_append_str(pool, buf, &v) != NGX_OK) {
            return NGX_ERROR;
        }

        return NGX_OK;
    }

    if (ngx_buf_append_char(pool, buf, '"') != NGX_OK) {
        return NGX_ERROR;
    }

    for (i = 0; i < string->len; i++) {
        c = string->data[i];

        if (c > 31 && c != '"' && c != '\\') {
            if (ngx_buf_append_char(pool, buf, c) != NGX_OK) {
                return NGX_ERROR;
            }
            continue;
        }

        //need to be escaped
        if (ngx_buf_append_char(pool, buf, '\\') != NGX_OK) {
            return NGX_ERROR;
        }

        switch (c) {
            case '\"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                if (ngx_buf_append_char(pool, buf, c) != NGX_OK) {
                    return NGX_ERROR;
                }
                break;
            default:
                snprintf(buffer, sizeof(buffer), "u%04x", c);
                v.data = (u_char *)buffer;
                v.len = sizeof(buffer);
                if (ngx_buf_append_str(pool, buf, &v) != NGX_OK) {
                return NGX_ERROR;
                }
                break;
        }
    }

    if (ngx_buf_append_char(pool, buf, '"') != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t json_print_type_string(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth) {
    return json_print_string(pool, format, &item->valuestring, buf, depth);
}

static ngx_int_t json_print_type_number(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth) {
    ngx_str_t value;
    double d, test = 0.0;
    char buffer[26] = {0};
    d = item->valuedouble;

    if (isnan(d) || isinf(d)) {
        ngx_str_set(&value, "null");
        if (ngx_buf_append_str(pool, buf, &value) != NGX_OK) {
            return NGX_ERROR;
        }
        return NGX_OK;
    }

    value.len = sprintf(buffer, "%1.15g", d);

    if (sscanf(buffer, "%lg", &test) != 1 || !compare_double(test, d)) {
        value.len = sprintf(buffer, "%1.17g", d);
    }

    value.data = (u_char *)buffer;

    if (ngx_buf_append_str(pool, buf, &value) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static ngx_int_t json_print_type_value(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_buf_t *buf, ngx_int_t *depth) {
    ngx_str_t value;

    switch (item->type) {
        case NGX_JSON_NULL:
            ngx_str_set(&value, "null");
            if (ngx_buf_append_str(pool, buf, &value) != NGX_OK) {
                return NGX_ERROR;
            }
            return NGX_OK;
        case NGX_JSON_TRUE:
            ngx_str_set(&value, "true");
            if (ngx_buf_append_str(pool, buf, &value) != NGX_OK) {
                return NGX_ERROR;
            }
            return NGX_OK;
        case NGX_JSON_FALSE:
            ngx_str_set(&value, "false");
            if (ngx_buf_append_str(pool, buf, &value) != NGX_OK) {
                return NGX_ERROR;
            }
        case NGX_JSON_NUMBER:
            return json_print_type_number(pool, format, item, buf, depth);
        case NGX_JSON_RAW:
        case NGX_JSON_STRING:
            return json_print_type_string(pool, format, item, buf, depth);
        case NGX_JSON_ARRAY:
            return json_print_type_array(pool, format, item, buf, depth);
        case NGX_JSON_OBJECT:
            return json_print_type_object(pool, format, item, buf, depth);
    }

    return NGX_ERROR;
}

static ngx_int_t json_print(ngx_pool_t *pool, bool format, ngx_json_t *item, ngx_str_t *v) {
    ngx_buf_t *buf;
    ngx_int_t depth = 0;
    buf = ngx_create_temp_buf(pool, 256);
    if (buf == NULL) {
        return NGX_ERROR;
    }
    
    if (json_print_type_value(pool, format, item, buf, &depth) != NGX_OK) {
        return NGX_ERROR;
    }
    
    assert(depth == 0);

    if (ngx_buf_to_str(pool, buf, v) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

ngx_int_t ngx_json_print(ngx_pool_t *pool, ngx_json_t *item, ngx_str_t *v) {
    return json_print(pool, true, item, v);
}

ngx_int_t ngx_json_unformated_print(ngx_pool_t *pool, ngx_json_t *item, ngx_str_t *v) {
    return json_print(pool, false, item, v);
}
