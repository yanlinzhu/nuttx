/****************************************************************************
 * drivers/clk/clk-fractional-divider.c
 *
 *   Copyright (C) 2017 Pinecone Inc. All rights reserved.
 *   Author: zhuyanlin<zhuyanlin@pinecone.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/clk/clk.h>
#include <nuttx/clk/clk-provider.h>
#include <nuttx/kmalloc.h>

#include <debug.h>

#include "clk.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define to_clk_fd(_clk) \
    (struct clk_fractional_divider *)(_clk->private_data)

/************************************************************************************
 * Private Functions
 ************************************************************************************/

static uint64_t gcd(uint64_t a, uint64_t b)
{
  uint64_t r;
  uint64_t tmp;

  if (a < b)
    {
      tmp = a;
      a = b;
      b = tmp;
    }

  if (!b)
    return a;
  while ((r = a % b) != 0)
    {
      a = b;
      b = r;
    }
  return b;
}

static uint64_t clk_fd_recalc_rate(struct clk *clk,
          uint64_t parent_rate)
{
  struct clk_fractional_divider *fd = to_clk_fd(clk);
  uint32_t val, m, n;
  uint64_t ret;

  val = clk_read(fd->reg);

  m = (val & fd->mmask) >> fd->mshift;
  n = (val & fd->nmask) >> fd->nshift;

  ret = parent_rate * m;
  ret /= (fd->flags & CLK_FRAC_DIV_DOUBLE ? 2 * n : n);

  return ret;
}

static int64_t clk_fd_round_rate(struct clk *clk, uint64_t rate,
            uint64_t *prate)
{
  struct clk_fractional_divider *fd = to_clk_fd(clk);
  uint32_t maxn = (fd->nmask >> fd->nshift) + 1;
  uint32_t maxm = (fd->mmask >> fd->mshift) + 1;
  uint32_t div, m, n;
  uint64_t ret;

  if (!rate || rate >= *prate)
    return *prate;

  if (fd->flags & CLK_FRAC_DIV_DOUBLE)
    rate *= 2;

  div = gcd(*prate, rate);

  do
    {
      m = rate / div;
      n = *prate / div;

      if ((m % 2 != 0) &&
          (fd->flags & CLK_FRAC_MUL_NEED_EVEN))
        {
          m *= 2;
          n *= 2;
        }

      div <<= 1;
    } while (n > maxn || m > maxm);

  ret = *prate * m;
  ret /= (fd->flags & CLK_FRAC_DIV_DOUBLE ? 2 * n : n);

  return ret;
}

static int clk_fd_set_rate(struct clk *clk, uint64_t rate,
         uint64_t parent_rate)
{
  struct clk_fractional_divider *fd = to_clk_fd(clk);
  uint64_t div;
  unsigned n, m;
  uint32_t val;

  if (fd->flags & CLK_FRAC_DIV_DOUBLE)
    rate *= 2;

  div = gcd(parent_rate, rate);
  m = rate / div;
  n = parent_rate / div;

  if ((m % 2 != 0) &&
      (fd->flags & CLK_FRAC_MUL_NEED_EVEN))
    {
      m *= 2;
      n *= 2;
    }

  val = clk_read(fd->reg);
  val &= ~(fd->mmask | fd->nmask);
  val |= (m << fd->mshift) | (n << fd->nshift);
  clk_write(fd->reg, val);

  return 0;
}

/************************************************************************************
 * Public Data
 ************************************************************************************/

const struct clk_ops clk_fractional_divider_ops =
{
  .recalc_rate = clk_fd_recalc_rate,
  .round_rate = clk_fd_round_rate,
  .set_rate = clk_fd_set_rate,
};

/************************************************************************************
 * Public Functions
 ************************************************************************************/

struct clk *clk_register_fractional_divider(const char *name,
    const char *parent_name, uint64_t flags,
    uint32_t reg, uint8_t mshift, uint8_t mwidth, uint8_t nshift, uint8_t nwidth,
    uint8_t clk_divider_flags)
{
  struct clk_fractional_divider *fd;
  struct clk *clk;
  int32_t num_parents;
  const char **parent_names;

  fd = kmm_malloc(sizeof(*fd));
  if (!fd)
    {
      clkerr("could not allocate fractional divider clk\n");
      return NULL;
    }

  parent_names = parent_name ? &parent_name : NULL;
  num_parents = parent_name ? 1 : 0;

  fd->reg = reg;
  fd->mshift = mshift;
  fd->mmask = (BIT(mwidth) - 1) << mshift;
  fd->nshift = nshift;
  fd->nmask = (BIT(nwidth) - 1) << nshift;
  fd->flags = clk_divider_flags;

  clk = clk_register(name, num_parents, parent_names, flags | CLK_IS_BASIC,
        &clk_fractional_divider_ops, fd);
  if (!clk)
    {
      kmm_free(fd);
    }

  return clk;
}