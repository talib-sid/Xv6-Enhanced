#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int main(int argc, char *argv[]){

    // Only go into the crux if the scheduling is set to be PBS
#ifdef PBS
    // Exit if the arguments are less than 3
    if(argc < 3){
        fprintf(2, "%s: Incorrect usage\n", argv[0]);
        exit(1);
    }

    // Fetch the priority

    int t = set_priority(atoi(argv[1]), atoi(argv[2]));
    if (t < 0) {
        fprintf(2, "%s: set_priority failed\n", argv[0]);
        exit(1);
    }
    else if (t == 101) {
        fprintf(2, "%s: pid %s not found\n", argv[2]);
        exit(1);
    }
#endif
    return 0;
    exit(0);
}