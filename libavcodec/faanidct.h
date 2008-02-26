void ff_faanidct(DCTELEM block[64]);
void ff_faanidct_add(uint8_t *dest, int line_size, DCTELEM block[64]);
void ff_faanidct_put(uint8_t *dest, int line_size, DCTELEM block[64]);
