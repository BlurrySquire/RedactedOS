#include "bin_mod.h"
#include "ping.h"
#include "shutdown.h"
#include "tracert.h"
#include "monitor_processes.h"
#include "kernel_processes/kprocess_loader.h"
#include "filesystem/filesystem.h"
#include "syscalls/syscalls.h"
#include "console/kio.h"
#include "process/loading/elf_file.h"
#include "input/input_dispatch.h"
#include "std/memory.h"
#include "process/scheduler.h"
#include "sysregs.h"

bool init_bin(){
    return true;
}

typedef struct open_bin_ref {
    char *name;
    int (*func)(int argc, char* argv[]);
} open_bin_ref;

open_bin_ref available_cmds[] = {
    { "ping", run_ping },
    { "shutdown", run_shutdown },
    { "tracert", run_tracert },
    { "monitor", monitor_procs },
};

process_t* load_proc(const char *full_name, const char *prog_name, int argc, const char *argv[]){
    file fd = {};
    if (!prog_name){
        prog_name = full_name;
        do {
            const char *next = seek_to(prog_name, '/');
            if (!*next) break;
            prog_name = next;
        } while (*prog_name);
    }
    string bundle = string_from_literal_length(full_name,prog_name-full_name-1);
    FS_RESULT op = openf(full_name, &fd);
    if (op != FS_RESULT_SUCCESS){
        kprintf("Failed to open file %s",full_name);
        return 0;
    }
    char *program = malloc(fd.size);
    if (readf(&fd, program, fd.size) != fd.size){
        kprintf("Failed to read file %s", full_name);
    }
    process_t *proc = load_elf_file(prog_name, bundle.data, program, fd.size);
    proc->win_id = get_current_proc()->win_id;
    closef(&fd);
    if (!proc){
        kprintf("Failed to create process for %s",prog_name);
    }
    proc->PROC_X0 = argc;
    if (argc > 0){
        uintptr_t start = (uintptr_t)argv[0];
        uintptr_t end = (uintptr_t)argv;
        size_t total = end-start;
        size_t argvs = argc * sizeof(uintptr_t);
        char *nargvals = (char*)(PHYS_TO_VIRT_P(proc->stack_phys)-total-argvs);
        char *vnargvals = (char*)(proc->stack-total-argvs);
        char** nargv = (char**)(PHYS_TO_VIRT_P(proc->stack_phys)-argvs);
        uintptr_t strptr = 0;
        for (int i = 0; i < argc; i++){
            size_t strsize = strlen(argv[i]);
            memcpy(nargvals + strptr, argv[i], strsize);
            *(char*)(nargvals + strptr + strsize++) = 0;
            nargv[i] = vnargvals + strptr;
            strptr += strsize;
        }
        proc->priority = PROC_PRIORITY_FULL;
        proc->PROC_X1 = (uintptr_t)proc->stack-argvs;
        proc->sp -= total+argvs;
    }
    proc->state = READY;
    sys_set_focus(proc->id);
    return proc;
}

process_t* execute(const char* prog_name, int argc, const char* argv[]){
    size_t listsize = 0x1000;
    void *listptr = zalloc(listsize);
    if (strcont(prog_name, "/")){
        return load_proc(prog_name, 0, argc, argv);
    }
    if (list_directory_contents("/boot/redos/bin/", listptr, listsize, 0)){
        char *full_name = strcat_new(prog_name, ".elf");
        string_list *list = (string_list*)listptr;
        char* reader = (char*)list->array;
        for (uint32_t i = 0; i < list->count; i++){
            char *f = reader;
            if (*f){
                if (strcmp_case(f, full_name,true) == 0){
                    string path = string_format("/boot/redos/bin/%s",full_name);
                    process_t *p = load_proc(path.data, full_name, argc, argv);
                    string_free(path);
                    release(full_name);
                    release(list);
                    return p;
                }
                while (*reader) reader++;
                reader++;
            }
        }
        release(full_name);
        release(listptr);
    }

    for (uint32_t i = 0; i < N_ARR(available_cmds); i++){
        if (strcmp(available_cmds[i].name, prog_name) == 0){
            return create_kernel_process(available_cmds[i].name, available_cmds[i].func, argc, argv);
        }
    }
    return 0;
}

FS_RESULT open_bin(){
    return FS_RESULT_DRIVER_ERROR;
}

size_t read_bin(){
    return 0;
}

size_t list_bin(const char *path, void *buf, size_t size, file_offset offset){
    return 0;
}

system_module bin_module = (system_module){
    .name = "bin",
    .mount = "/bin",
    .version = VERSION_NUM(0, 1, 0, 1),
    .init = init_bin,
    .fini = 0,
    .open = 0,
    .read = 0,
    .write = 0,
    .sread = 0,
    .swrite = 0,
    .readdir = 0,
};//TODO: with dfs, should be possible to map virts for hardcoded commands + physical map to /boot/redos/bin
