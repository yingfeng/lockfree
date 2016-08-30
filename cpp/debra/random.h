#ifndef RANDOM_H
#define	RANDOM_H

class Random {
private:
    unsigned int seed;
public:
    Random(void) {
        this->seed = 0;
    }
    Random(int seed) {
        this->seed = seed;
    }
    
    void setSeed(int seed) {
        this->seed = seed;
    }

    /** returns pseudorandom x satisfying 0 <= x < n. **/
    int nextNatural(int n) {
        seed ^= seed << 6;
        seed ^= seed >> 21;
        seed ^= seed << 7;
        int retval = (int) (seed % n);
        return (retval < 0 ? -retval : retval);
    }

};

#endif	/* RANDOM_H */

