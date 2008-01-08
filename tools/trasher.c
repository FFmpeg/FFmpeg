
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>

int main(int argc, char** argv)
{
    FILE *f= fopen(argv[1], "rb+");
    int count= atoi(argv[2]);
    int maxburst= atoi(argv[3]);
    int length, i;

    srand (time (0));

    fseek(f, 0, SEEK_END);
    length= ftell(f);
    fseek(f, 0, SEEK_SET);

    while(count--){
        int burst= 1 + random() * (uint64_t) (abs(maxburst)-1) / RAND_MAX;
        int pos= random() * (uint64_t) length / RAND_MAX;
        fseek(f, pos, SEEK_SET);

        if(maxburst<0) burst= -maxburst;

        if(pos + burst > length)
            continue;

        while(burst--){
            int val= random() * 256ULL / RAND_MAX;

            if(maxburst<0) val=0;

            fwrite(&val, 1, 1, f);
        }
    }

    return 0;
}
