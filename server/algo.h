/* Copyright (C) 2000-2019 by Massimiliano Ghilardi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#ifndef _TWIN_ALGO_H
#define _TWIN_ALGO_H

INLINE ldat Abs(ldat x) {
  return x >= 0 ? x : -x;
}
INLINE dat Sign(ldat x) {
  return x > 0 ? 1 : x < 0 ? -1 : 0;
}
INLINE ldat Min2(ldat x, ldat y) {
  return x < y ? x : y;
}
INLINE ldat Max2(ldat x, ldat y) {
  return x > y ? x : y;
}
INLINE ldat Max3(ldat x, ldat y, ldat z) {
  return x > y ? Max2(x, z) : Max2(y, z);
}

#endif /* _TWIN_ALGO_H */
