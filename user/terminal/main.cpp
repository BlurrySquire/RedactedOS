#include "terminal.hpp"
#include "string/string.h"

Terminal *term;

int main(int argc, char **argv){
    term = new Terminal();
    if (argc && strncmp_case(argv[0], "headless", true, 8) == 0){
        print("Terminal in headless mode");
        term->headless = true; 
    }
    while (1){
        term->update();
    }
}