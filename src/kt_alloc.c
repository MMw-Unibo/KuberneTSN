#include "kt_alloc.h"
#include "kt_logger.h"

//------------------------------------------------------------------------------
void page_alloc_print_stats(struct kt_allocator *alloc)
{
    struct kt_page_allocator *page_alloc = (struct kt_page_allocator *)alloc->data;
    printf("page_size: %d\n", page_alloc->page_size);
    printf("page_count: %d\n", page_alloc->page_count);
    printf("page_free: %d\n", page_alloc->page_free);
    printf("page_list: %p\n", (void*)page_alloc->page_list);
    printf("free_pages_mask: %p\n", page_alloc->free_pages_mask);
    printf("data: %p\n", page_alloc->data);
    printf("\n");
}

//------------------------------------------------------------------------------
int kt_page_allocator_init(u8 *data, u32 size, u32 page_size)
{
    // check if size AND page_size are powers of 2
    if ((size & (size - 1)) != 0 || (page_size & (page_size - 1)) != 0)
    {
        LOG_DEBUG("size and page_size must be powers of 2\n");
        return -1;
    }

    u32 page_count = size / page_size;
    u32 page_list_size;
    u32 free_pages_mask_size;
    u32 total_metadata_size;
    do
    {
        page_list_size = page_count * sizeof(struct kt_page);
        free_pages_mask_size = page_count * sizeof(u8);
        total_metadata_size = sizeof(struct kt_page_allocator) + page_list_size + free_pages_mask_size;

        page_count--;
        if (page_count == 0)
        {
            LOG_DEBUG("size is too small to fit the metadata\n");
            return -1;
        }
    } while (size < total_metadata_size);

    LOG_TRACE("total_metadata_size: %d\n", total_metadata_size);

    u8 *page_list_offset = data + sizeof(struct kt_page_allocator);
    u8 *free_pages_mask_offset = page_list_offset + page_list_size * sizeof(struct kt_page);
    u8 *data_offset = free_pages_mask_offset + free_pages_mask_size * sizeof(u8);

    // update the data offset pointer to be aligned to page size
    // NOTE(garbu): before there was a bug here, the page_size was casted to u32
    // TODO(garbu): change the type of page_size to size_t and remove the cast 
    size_t page_size_value = (size_t)page_size;
    data_offset = (u8 *)(((size_t)data_offset + page_size_value - 1) / page_size_value * page_size_value);

    // create the alloc
    struct kt_page_allocator *alloc = (struct kt_page_allocator *)data;
    alloc->page_size = page_size;
    alloc->page_count = page_count;
    alloc->page_free = page_count;
    alloc->page_list = (struct kt_page *)page_list_offset;
    alloc->free_pages_mask = free_pages_mask_offset;
    alloc->data = data_offset;

    // initialize the pages and    set the last page's next pointer to first page
    for (u32 i = 0; i < page_count; i++)
    {
        alloc->page_list[i].next = &alloc->page_list[i + 1];
        alloc->page_list[i].free = 1;
        alloc->page_list[i].offset = i * page_size;
        alloc->page_list[i].reserved = 0;
        alloc->free_pages_mask[i] = 1;
    }
    alloc->page_list[page_count - 1].next = &alloc->page_list[0];

    return 0;
}

//---------------------------------------------------------------------------------------------
static struct kt_page *
page_alloc_search_first_free_page_from(struct kt_page_allocator *alloc, u32 start, u32 *index)
{
    if (start >= alloc->page_count)
    {
        return NULL;
    }

    for (u32 i = start; i < alloc->page_count; i++)
    {
        if (alloc->free_pages_mask[i])
        {
            if (index != NULL)
                *index = i;
            return &alloc->page_list[i];
        }
    }

    return NULL;
}

static struct kt_page *
page_alloc_search_first_free_page(struct kt_page_allocator *alloc, u32 *index)
{
    return page_alloc_search_first_free_page_from(alloc, 0, index);
}

/**
 * @brief page_alloc_find_aligned_pages() - Finds and returns a page in the memory managed by the
 * page allocator that is properly aligned for the requested number of pages
 *
 * @alloc: The page allocator
 * @pages_needed: The number of pages needed
 * @index: A pointer to an integer representing the current starting index of the page being
 * searched for. The value will be updated by the function to represent the index of the found page
 *
 * @return A pointer to the found page, or NULL if no page was found
 */
static struct kt_page *
page_alloc_find_aligned_pages(struct kt_page_allocator *alloc, u32 pages_needed, u32 *index)
{
    // Reset the index of the current page being scanned
    *index = 0;

    // Get the pointer to the first free page in the alloc
    struct kt_page *page;
    while ((page = page_alloc_search_first_free_page_from(alloc, *index, index)) != NULL)
    {

        // If we only need one page, return it
        if (pages_needed == 1)
        {
            return page;
        }

        // Every loop iteration checks the next pages until either the desired number is found or a
        // non-free page is encountered
        for (u32 page_found = 1; page_found < pages_needed; page_found++)
        {
            u32 k = *index + page_found;

            // If the current page is within range and is also set to be free, check if all pages
            // are now connected
            if (k < alloc->page_count && alloc->free_pages_mask[k])
            {
                if ((page_found + 1) == pages_needed)
                {
                    return page;
                }
                // Else update the index to the current page and start checking again
            }
            else
            {
                *index = k;
                break;
            }
        }
    }

    // Block not found, return NULL
    return NULL;
}

void *
page_alloc_alloc(struct kt_allocator *_alloc, u32 size)
{
    // get the page allocator
    struct kt_page_allocator *alloc = (struct kt_page_allocator *)_alloc->data;

    // caluculate the number of pages needed
    u32 pages_needed = size / alloc->page_size + (size % alloc->page_size != 0);

    LOG_DEBUG("allocating %d pages\n", pages_needed);

    // check if there are enough pages
    if (alloc->page_free < pages_needed)
    {
        LOG_DEBUG("not enough pages\n");
        return NULL;
    }

    // check if there are enough aligned pages to satisfy the memory request or try to find a new
    // free page
    u32 index;
    struct kt_page *page = page_alloc_find_aligned_pages(alloc, pages_needed, &index);

    // if we didn't find enough pages, return NULL
    if (page == NULL)
    {
        LOG_DEBUG("not enough aligned pages\n");
        return NULL;
    }

    struct kt_page *tmp = page;
    page->reserved = pages_needed;
    for (u32 i = 0; i < pages_needed; i++)
    {
        tmp->free = 0;
        tmp = tmp->next;
    }

    // set used pages mask
    for (u32 i = index; i < index + pages_needed; i++)
    {
        alloc->free_pages_mask[i] = 0;
    }

    // update the number of free pages
    alloc->page_free -= pages_needed;

    // return the pointer to the first page
    u8* ptr = alloc->data + page->offset;

    LOG_TRACE("allocated %d pages at %p\n", pages_needed, ptr);
    return ptr;
}

void page_alloc_free(struct kt_allocator *_alloc, void *ptr)
{
    // get the page allocator
    struct kt_page_allocator *alloc = (struct kt_page_allocator *)_alloc->data;

    // get the page index
    u32 index = ((u8 *)ptr - alloc->data) / alloc->page_size;

    // get the page
    struct kt_page *page = &alloc->page_list[index];

    // check if the page is free
    if (page->free)
    {
        return;
    }

    // set the page as free
    page->free = 1;
    struct kt_page *tmp = page;
    for (u32 i = 0; i < page->reserved; i++)
    {
        tmp->free = 1;
        tmp = tmp->next;
    }

    // update the number of free pages
    alloc->page_free += page->reserved;

    // set used pages mask
    for (u32 i = index; i < index + page->reserved; i++)
    {
        alloc->free_pages_mask[i] = 1;
    }

    // reset the reserved pages
    page->reserved = 0;
}

//---------------------------------------------------------------------------------------------
struct kt_allocator *
kt_page_allocator_make(u8 *data, u32 size, u32 page_size)
{
    LOG_DEBUG("Making page allocator at %p with size %d and page size %d\n", data, size, page_size);

    struct kt_allocator *al = (struct kt_allocator *)malloc(sizeof(struct kt_allocator));
    al->data = data;
    al->alloc = page_alloc_alloc;
    al->free = page_alloc_free;
    al->print_stats = page_alloc_print_stats;

    kt_page_allocator_init(al->data, size, page_size);

    return al;
}