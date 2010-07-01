#ifndef KALMAN_H__
#define KALMAN_H__


/**
 *
 */
typedef struct {
  double x_next;
  double P, K, Q, R;
} kalman_t;


/**
 *
 */
static inline void
kalman_init(kalman_t *k)
{
  k->P = 1.0;
  k->Q = 1.0 / 100000.0;
  k->R = 0.01;
  k->x_next = 0.0;
}


/**
 *
 */
static inline double
kalman_update(kalman_t *k, double z)
{
  double x, P_next;

  P_next = k->P + k->Q;
  k->K = P_next / (P_next + k->R);
  x = k->x_next + k->K * (z - k->x_next);
  k->P = (1 - k->K) * P_next;
  return k->x_next = x;
}

#endif // KALMAN_H__
