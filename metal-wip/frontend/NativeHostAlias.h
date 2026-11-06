// SPDX-License-Identifier: MIT
// Keep a shared alias of the original pages for an asynchronous Metal no-copy buffer.
#ifndef NATIVE_HOST_ALIAS_H
#define NATIVE_HOST_ALIAS_H
#include <mach/mach.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    vm_address_t mapping;
    vm_size_t mappingBytes;
    void *bytes;
} NBHostAlias;

static inline kern_return_t NBHostAliasCreate(void *pointer,size_t length,NBHostAlias *result)
{
    *result=(NBHostAlias){0,0,NULL};
    uintptr_t page=(uintptr_t)vm_page_size,address=(uintptr_t)pointer;
    if(!address || !length || !page || (page&(page-1)))return KERN_INVALID_ARGUMENT;
    uintptr_t leading=address&(page-1);
    if(length>UINTPTR_MAX-address || length>UINTPTR_MAX-leading ||
        length+leading>UINTPTR_MAX-(page-1))return KERN_INVALID_ARGUMENT;
    vm_size_t size=(length+leading+page-1)&~(page-1);
    vm_address_t alias=0;
    vm_prot_t current=VM_PROT_NONE,maximum=VM_PROT_NONE;
    kern_return_t status=vm_remap(mach_task_self(),&alias,size,0,VM_FLAGS_ANYWHERE,
        mach_task_self(),address-leading,FALSE,&current,&maximum,VM_INHERIT_NONE);
    if(status!=KERN_SUCCESS)return status;
    if(!(current&VM_PROT_READ)) {
        (void)vm_deallocate(mach_task_self(),alias,size);
        return KERN_PROTECTION_FAILURE;
    }
    result->mapping=alias;result->mappingBytes=size;result->bytes=(void *)(alias+leading);
    return KERN_SUCCESS;
}

static inline void NBHostAliasDestroy(NBHostAlias *alias)
{
    if(alias->mapping)(void)vm_deallocate(mach_task_self(),alias->mapping,alias->mappingBytes);
    *alias=(NBHostAlias){0,0,NULL};
}
#endif
