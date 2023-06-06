#ifndef __NGX_JSON__H__
#define __NGX_JSON__H__


#include <ngx_config.h>
#include <ngx_core.h>


#define NGX_JSON_INVALID -1
#define NGX_JSON_FALSE   (1 << 0)
#define NGX_JSON_TRUE    (1 << 1)
#define NGX_JSON_NULL    (1 << 2)
#define NGX_JSON_NUMBER  (1 << 3)
#define NGX_JSON_STRING  (1 << 4)
#define NGX_JSON_ARRAY   (1 << 5)
#define NGX_JSON_OBJECT  (1 << 6)
#define NGX_JSON_RAW     (1 << 7)



typedef struct ngx_json_s
{
    ngx_queue_t list;
    ngx_int_t type;
    ngx_str_t key;
    
    ngx_int_t valueint;
    double    valuedouble;
    ngx_str_t valuestring;
} ngx_json_t;


ngx_json_t *ngx_json_parse(ngx_pool_t *pool, ngx_str_t v);

ngx_int_t ngx_json_print(ngx_pool_t *pool, ngx_json_t *item, ngx_str_t *v);
ngx_int_t ngx_json_unformated_print(ngx_pool_t *pool, ngx_json_t *item, ngx_str_t *v);


ngx_int_t ngx_json_delete(ngx_pool_t *pool, ngx_json_t *c);
ngx_int_t ngx_json_add_item_to_object(ngx_pool_t *pool, ngx_json_t *obj, ngx_str_t *key, ngx_json_t *item);
ngx_int_t ngx_json_add_item_to_array(ngx_pool_t *pool, ngx_json_t *array, ngx_json_t *value);



ngx_json_t *ngx_json_new(ngx_pool_t *pool, ngx_int_t type);

ngx_json_t *ngx_json_new_null(ngx_pool_t *pool);
ngx_json_t *ngx_json_new_number(ngx_pool_t *pool, double num);
ngx_json_t *ngx_json_new_string(ngx_pool_t *pool, ngx_str_t *value);


ngx_json_t *ngx_json_new_obj(ngx_pool_t *pool);
ngx_json_t *ngx_json_new_array(ngx_pool_t *pool);

#define ngx_json_add_number_to_object(pool, obj, key, n) ngx_json_add_item_to_object(pool, obj, key, ngx_json_new_number(pool, n))
#define ngx_json_add_string_to_object(pool, obj, key, s) ngx_json_add_item_to_object(pool, obj, key, ngx_json_new_string(pool, s))

#endif  /*__NGX_JSON__H__*/