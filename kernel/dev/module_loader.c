#include "module_loader.h"
#include "console/kio.h"
#include "filesystem/filesystem.h"
#include "sysregs.h"
#include "memory/page_allocator.h"

//TODO: use hashmaps
linked_list_t* modules;
void *mod_page = 0;

void* mod_alloc(size_t size){
    if (!mod_page) mod_page = palloc(PAGE_SIZE, MEM_PRIV_KERNEL, MEM_RW, false);
    return kalloc(mod_page, size, ALIGN_16B, MEM_PRIV_KERNEL);
}

void mod_free(void* ptr){
    kfree(ptr, sizeof(linked_list_node_t));
}

bool load_module(system_module *module){
    if (!modules){   
        modules = linked_list_create();
        modules->alloc = mod_alloc;
        modules->free = mod_free;
    }
    if (!module->init){
        if (strcmp(module->mount,"/console")) kprintf("[MODULE] module not initialized due to missing initializer");//TODO: can we make printf silently fail so logging becomes easier?
        return false;
    }
    if (!module->init()){
        if (strcmp(module->mount,"/console")) kprintf("[MODULE] failed to load module %s. Init failed",module->name);
        return false;
    }
    linked_list_push_front(modules, PHYS_TO_VIRT_P(module));
    return true;
}

int fs_search(void *node, void *key){
    system_module* module = (system_module*)node;
    if (!module) return -1;
    kprint(module->mount);
    const char** path = (const char**)key;
    int index = strstart_case(*path, module->mount,true);
    if (index == (int)strlen(module->mount)){ 
        *path += index;
        return 0;
    }
    return -1;
}

bool unload_module(system_module *module){
    if (!modules) return false;
    if (!module->init) return false;
    if (module->fini) module->fini();
    const char *name = module->mount;
    linked_list_node_t *node = linked_list_find(modules, (void*)&name, fs_search);
    linked_list_remove(modules, node);
    return false;
}

system_module* get_module(const char **full_path){
    linked_list_node_t *node = linked_list_find(modules, (void*)full_path, fs_search);
    return node ? ((system_module*)node->data) : 0;
}
