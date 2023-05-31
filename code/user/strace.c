#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    char *args[32];

    if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9')){             // Case where mask has charecters other than numerical digits
        fprintf(2, "Usage: %s mask command\n", argv[0]);            // writing into the stderr buffer
        exit(1);
    }

    if (trace(atoi(argv[1])) < 0) {
        fprintf(2, "%s: negative mask value given, trace failed\n", argv[0]);
        exit(1);
    }

    for(int i = 2; i < argc && i < 32; i++){
    	args[i-2] = argv[i];               // The command is isolated
    }

    exec(args[0], args);

    exit(0);
}