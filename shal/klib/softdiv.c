int __mulsi3(int a, int b) {
    int result = 0;
    while (b > 0) {
        if (b & 1) result += a;
        a <<= 1;
        b >>= 1;
    }
    return result;
}

unsigned int __umulsi3(unsigned int a, unsigned int b) {
    unsigned int result = 0;
    while (b > 0) {
        if (b & 1) result += a;
        a <<= 1;
        b >>= 1;
    }
    return result;
}

int __divsi3(int a, int b) {
    int result = 0;
    int sign = 1;
    if (a < 0) { a = -a; sign = -sign; }
    if (b < 0) { b = -b; sign = -sign; }
    while (a >= b) {
        a -= b;
        result++;
    }
    return sign * result;
}

unsigned int __udivsi3(unsigned int a, unsigned int b) {
    unsigned int result = 0;
    while (a >= b) {
        a -= b;
        result++;
    }
    return result;
}

int __modsi3(int a, int b) {
    int sign = 1;
    if (a < 0) { a = -a; sign = -sign; }
    if (b < 0) b = -b;
    while (a >= b) a -= b;
    return sign * a;
}

unsigned int __umodsi3(unsigned int a, unsigned int b) {
    while (a >= b) a -= b;
    return a;
}
