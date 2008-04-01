
#ifndef YADIF_H__
#define YADIF_H__

int yadif_init(void);

extern void (*yadif_filter_line)(int mode, uint8_t *dst, uint8_t *prev, uint8_t *cur, uint8_t *next, int w, int refs, int parity);

#endif /* YADIF_H__ */
