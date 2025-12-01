#include <string.h>

void memcpy(uint8_t *dest, const uint8_t *src, uint32_t len)
{
    for (; len != 0; len--) {
        *dest++ = *src++;
    }
}

void memset(void *dest, uint8_t val, uint32_t len)
{
    uint8_t *dst = (uint8_t *)dest;

    for (; len != 0; len--) {
        *dst++ = val;
    }
}

void bzero(void *dest, uint32_t len)
{
    memset(dest, 0, len);
}

int strlen(const char *src)
{
    int i = 0;
    while (src[i] != '\0') {
        i++;
    }
    return i;
}

int strcmp(const char *str1, const char *str2)
{
    while (*str1 && *str2) {
        if (*str1 != *str2) {
            return (*str1) - (*str2);
        }
        ++str1;
        ++str2;
    }
    return (*str1) - (*str2);
}

int strncmp(const char *str1, const char *str2, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        if (str1[i] == '\0' || str1[i] != str2[i])
            return str1[i] - str2[i];
    return 0;
}

char *strcpy(char *dest, const char *src)
{
    char *tmp = dest;

    while (*src) {
        *dest++ = *src++;
    }

    *dest = '\0';

    return tmp;
}

char *strncpy(char *dest, const char *src, int n)
{
    char *tmp = dest;

    while (*src && n-- > 0) {
        *dest++ = *src++;
    }

    while (n-- > 0) {
        *dest++ = '\0';
    }

    return tmp;
}

char *strcat(char *dest, const char *src)
{
    char *tmp = dest;

    while (*dest != '\0') {
        dest++;
    }
    while (*src) {
        *dest++ = *src++;
    }

    *dest = '\0';

    return tmp;
}

void strrev(char *str)
{
    int i, j;
    int len = strlen(str);

    for (i = 0, j = len - 1; i < j; i++, j--)
    {
        char tmp = str[i];
        str[i] = str[j];
        str[j] = tmp;
    }
}

unsigned long long strtoull_simple(const char *s, int base)
{
    while (*s == ' ' || *s == '\t')
        s++;
    int neg = 0;
    if (*s == '+') s++;
    else if (*s == '-') { neg = 1; s++; }

    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
            s++;
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            s += 2;
    }

    unsigned long long val = 0, prev = 0;
    const unsigned long long ULL_MAX_VAL = ~0ULL;

    while (*s) {
        int digit;

        if (*s >= '0' && *s <= '9')
            digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f')
            digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F')
            digit = *s - 'A' + 10;
        else
            break;  

        if (digit >= base)
            break;

        prev = val;
        val = val * base + digit;

        if (val < prev) {
            val = ULL_MAX_VAL;    
            break;
        }

        s++;
    }

    return neg ? -val : val;
}
