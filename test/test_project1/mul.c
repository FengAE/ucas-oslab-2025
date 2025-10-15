#include<kernel.h>

int main(int argc, char* argv[])
{
    int data;
    if(argv[1][0] == '1')   // bat
        data = *(int*)BATCH_DATA_LOC;
    else    // not bat
        data = 2;
    int ret = data * 3;
    int ret_tmp = ret;
    char buffer[10], in_buffer[10];
    int i=0, j=0;
    for(; ret_tmp>0; ret_tmp /=10)
        buffer[i++] = ret_tmp%10 + '0';
    for(; data>0; data /= 10)
        in_buffer[j++] = data%10 + '0';

    for(int left=0, right=i-1; left < right; left++, right--)
    {
        char tmp = buffer[left];
        buffer[left] = buffer[right];
        buffer[right] = tmp;
    }
    for(int left=0, right=j-1; left < right; left++, right--)
    {
        char tmp = in_buffer[left];
        in_buffer[left] = in_buffer[right];
        in_buffer[right] = tmp;
    }
    buffer[i] = '\0';
    in_buffer[j] = '\0';

    bios_putstr("[mul] Info: input data: ");
    bios_putstr(in_buffer);
    bios_putstr("\n\r");
    bios_putstr("[mul] Info: mul 3 result: ");
    bios_putstr(buffer);
    bios_putstr("\n\r");

    if(argv[1][0] == '1')
        *(int*)BATCH_DATA_LOC = ret;

    return 0;
}