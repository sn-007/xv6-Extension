#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(int argc, char *argv[])
{
    if (argc !=3)
    {
        printf("wrong usage\n");
        exit(0);
    }

    int priority = atoi(argv[1]);
    int pid = atoi(argv[2]);


    printf("%d\n",setpriority(priority,pid));

exit(0);
}
