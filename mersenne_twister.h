// https://www.literateprograms.org/mersenne_twister__c_.html

#define MT_LEN 624
#define MT_IA           397
#define MT_IB           (MT_LEN - MT_IA)
#define UPPER_MASK      0x80000000
#define LOWER_MASK      0x7FFFFFFF
#define MATRIX_A        0x9908B0DF
#define TWIST(b,i,j)    ((b)[i] & UPPER_MASK) | ((b)[j] & LOWER_MASK)
#define MAGIC(s)        (((s)&1)*MATRIX_A)

typedef struct {
	uint32_t buffer[MT_LEN];
	short index;
} mt_state;

void mt_init(mt_state* mt, size_t seed) {
	mt->index = 0;
	seed += 0xdead;
    for (uint32_t i = 0; i < MT_LEN; i++) {
		uint32_t t = (seed*seed*seed + i*i*i);
        mt->buffer[i] = t*t*t;
	}
}

uint32_t mt_random(mt_state* mt) {
    uint32_t s;
    short i;
    if (UNLIKELY(mt->index++ == MT_LEN)) {
		mt->index = 0;
		i = 0;
		for (; i < MT_IB; i++) {
			s = TWIST(mt->buffer, i, i+1);
			mt->buffer[i] = mt->buffer[i + MT_IA] ^ (s >> 1) ^ MAGIC(s);
		}
		for (; i < MT_LEN-1; i++) {
			s = TWIST(mt->buffer, i, i+1);
			mt->buffer[i] = mt->buffer[i - MT_IB] ^ (s >> 1) ^ MAGIC(s);
		}
		s = TWIST(mt->buffer, MT_LEN-1, 0);
		mt->buffer[MT_LEN-1] = mt->buffer[MT_IA-1] ^ (s >> 1) ^ MAGIC(s);
	}
	return mt->buffer[mt->index];
}
