#include <fcntl.h>
#include <sys/mman.h>

#include "kt_logger.h"
#include "kt_memory.h"

//--------------------------------------------------------------------------------------------------
static struct kt_memory *_kt_memory_alloc(const char *name, size_t size, i32 oflags, mode_t omode)
{
    struct kt_memory *memory = NULL;

    memory = malloc(sizeof(struct kt_memory));
    if (!memory)
    {
        LOG_ERROR("%s (%s) - cannot allocate memory\n", __func__, __FILE__);
        return NULL;
    }

    memory->size = size;
    strncpy(memory->name, name, KT_MEMORY_NAMESIZE - 1);

    memory->fd = shm_open(name, oflags, omode);
    if (memory->fd == -1)
    {
        LOG_ERROR("%s (%s) - cannot open shared memory: %s\n", __func__, __FILE__, strerror(errno));
        goto err;
    }

    if (ftruncate(memory->fd, size))
    {
        LOG_ERROR("%s (%s) - truncate failed: %s\n", __func__, __FILE__, strerror(errno));
        goto err;
    }

    memory->addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, memory->fd, 0);
    if (memory->addr == MAP_FAILED)
    {
        LOG_ERROR("%s (%s) - mmap failed: %s\n", __func__, __FILE__, strerror(errno));
        goto err;
    }

    memory->used = 0;

    return memory;

err:
    free(memory);
    return NULL;
}

//--------------------------------------------------------------------------------------------------
struct kt_memory *kt_memory_attach(const char *name, size_t size)
{
    struct kt_memory *memory = NULL;

    memory = _kt_memory_alloc(name, size, O_RDWR, 0);
    if (!memory)
    {
        LOG_ERROR("%s (%s) - cannot allocate shared memory\n", __func__, __FILE__);
        return NULL;
    }

    return memory;
}

//--------------------------------------------------------------------------------------------------
struct kt_memory *kt_memory_create(const char *name, size_t size)
{
    struct kt_memory *memory = NULL;

    memory = _kt_memory_alloc(name, size, O_CREAT | O_EXCL | O_RDWR, S_IRUSR);
    if (!memory)
    {
        LOG_ERROR("%s (%s) - cannot allocate shared memory\n", __func__, __FILE__);
        return NULL;
    }

    return memory;
}

//--------------------------------------------------------------------------------------------------
i32 kt_memory_destroy(struct kt_memory *m)
{
    munmap(m->addr, m->size);
    shm_unlink(m->name);
    close(m->fd);
    free(m);

    return 0;
}

//--------------------------------------------------------------------------------------------------
i32 kt_memory_detach(struct kt_memory *m, int (*_close)(int))
{
    if (munmap(m->addr, m->size) < 0)
    {
        LOG_ERROR("munmap\n");
        goto error;
    }

    if (_close(m->fd) < 0)
    {
        LOG_ERROR("close\n");
        goto error;
    }

    free(m);

    return 0;

error:
    return -1;
}
