/*
 * Copyright (c) 2017-2024 Arm Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "wsialloc.h"
#include "wsialloc_helpers.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ion.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

/**
 * @brief Version of the wsialloc interface we are implementing in this file.
 *
 * This should only be increased when this implementation is updated to match newer versions of wsialloc.h.
 */
#define WSIALLOC_IMPLEMENTATION_VERSION 3

/* Ensure we are implementing the wsialloc version matching the wsialloc.h header we are using. */
#if WSIALLOC_IMPLEMENTATION_VERSION != WSIALLOC_INTERFACE_VERSION
#error "Version mismatch between wsialloc implementation and interface version"
#endif

const uint32_t WSIALLOC_IMPLEMENTATION_VERSION_SYMBOL = WSIALLOC_IMPLEMENTATION_VERSION;

struct wsialloc_allocator
{
   /* File descriptor of /dev/ion. */
   int fd;
   /* Allocator heap id. */
   uint32_t alloc_heap_id;
   /* Protected allocator heap id */
   uint32_t protected_alloc_heap_id;
   bool protected_heap_exists;
};

static int find_alloc_heap_id(int fd)
{
   assert(fd != -1);

   struct ion_heap_data heaps[ION_NUM_HEAP_IDS];
   struct ion_heap_query query = {
      .cnt = ION_NUM_HEAP_IDS,
      .heaps = (uint64_t)(uintptr_t)heaps,
   };

   int ret = ioctl(fd, ION_IOC_HEAP_QUERY, &query);
   if (ret < 0)
   {
      return ret;
   }

   int alloc_heap_id = -1;
   for (uint32_t i = 0; i < query.cnt; ++i)
   {
      if (ION_HEAP_TYPE_DMA == heaps[i].type)
      {
         alloc_heap_id = heaps[i].heap_id;
         break;
      }
   }

   return alloc_heap_id;
}

static int allocate(int fd, size_t size, uint32_t heap_id)
{
   assert(size > 0);
   assert(fd != -1);

   struct ion_allocation_data alloc = {
      .len = size,
      .heap_id_mask = 1u << heap_id,
      .flags = 0,
   };
   int ret = ioctl(fd, ION_IOC_ALLOC, &alloc);
   if (ret < 0)
   {
      return ret;
   }

   return alloc.fd;
}

wsialloc_error wsialloc_new(wsialloc_allocator **allocator)
{
   assert(allocator != NULL);
   wsialloc_allocator *ion = malloc(sizeof(wsialloc_allocator));
   if (NULL == ion)
   {
      wsialloc_delete(ion);
      return WSIALLOC_ERROR_NO_RESOURCE;
   }

   ion->fd = open("/dev/ion", O_RDONLY);
   if (ion->fd < 0)
   {
      wsialloc_delete(ion);
      return WSIALLOC_ERROR_NO_RESOURCE;
   }

   ion->alloc_heap_id = find_alloc_heap_id(ion->fd);
   if (ion->alloc_heap_id < 0)
   {
      wsialloc_delete(ion);
      return WSIALLOC_ERROR_NO_RESOURCE;
   }

   ion->protected_heap_exists = false;
   *allocator = ion;
   return WSIALLOC_ERROR_NONE;
}

void wsialloc_delete(wsialloc_allocator *allocator)
{
   assert(allocator != NULL);
   if (NULL == allocator)
   {
      return;
   }

   if (allocator->fd >= 0)
   {
      close(allocator->fd);
      allocator->fd = -1;
   }

   free(allocator);
}

static int ion_allocate(const wsialloc_allocator *allocator, const wsialloc_allocate_info *info, uint64_t size)
{
   assert(allocator != NULL);
   assert(info != NULL);
   assert(allocator->fd != -1);
   assert(size > 0);

   /* The only error that can be encountered on allocations is lack of resources. Other parameter validation and
    * support checks are done on format selection. */
   uint32_t alloc_heap_id = allocator->alloc_heap_id;
   if (info->flags & WSIALLOC_ALLOCATE_PROTECTED)
   {
      /* Exit if we don't support allocating protected memory. */
      if (!allocator->protected_heap_exists)
      {
         assert(false);
         return -1;
      }
      alloc_heap_id = allocator->protected_alloc_heap_id;
   }

   return allocate(allocator->fd, size, alloc_heap_id);
}

wsialloc_error wsialloc_alloc(wsialloc_allocator *allocator, const wsialloc_allocate_info *info,
                              wsialloc_allocate_result *result)
{
   if ((info->flags & WSIALLOC_ALLOCATE_PROTECTED) && (!allocator->protected_heap_exists))
   {
      return WSIALLOC_ERROR_NO_RESOURCE;
   }

   return wsiallocp_alloc(allocator, ion_allocate, info, result);
}
