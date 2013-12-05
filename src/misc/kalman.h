/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
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
