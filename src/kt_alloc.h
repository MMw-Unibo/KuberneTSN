#ifndef KT_ALLOC_H
#define KT_ALLOC_H

#include "kt_common.h"

struct kt_allocator {
    void *data;
    void *(*alloc)(struct kt_allocator *alloc, u32 size);
    void (*free)(struct kt_allocator *alloc, void *ptr);
    void (*print_stats)(struct kt_allocator *alloc);
};

struct kt_page
{
    struct kt_page *next;
    u32 free;
    u32 offset;
    // number of pages reserved starting from this page
    u32 reserved;
};

/* Page allocator structure
 * |-------------------------------------|
 * | struct page_alloc                   |
 * |     struct page *page_list          |
 * |     u8 *free_pages_mask             |
 * |     u8 *data                        |
 * |-------------------------------------|
 * | struct page 0                       |
 * |     struct page *next               |
 * |     u32 size                        |
 * |     u32 free                        |
 * |     u32 offset                      |
 * |-------------------------------------|
 * | struct page N                       |
 * |     struct page *next               |
 * |     u32 size                        |
 * |     u32 free                        |
 * |     u32 offset                      |
 * |-------------------------------------|
 * | u8 free_pages_mask 0                |
 * |-------------------------------------|
 * | u8 free_pages_mask N                |
 * |-------------------------------------|
 * | u8 data 0                           |
 * |-------------------------------------|
 * | u8 data N                           |
 * |-------------------------------------|
 *
 */
struct kt_page_allocator
{
    u32 page_size;
    u32 page_count;
    u32 page_free;
    struct kt_page *page_list;
    u8 *free_pages_mask;
    u8 *data;
};

struct kt_allocator *kt_page_allocator_make(u8 *data, u32 size, u32 page_size);

#endif // KT_ALLOC_H