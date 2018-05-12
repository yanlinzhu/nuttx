/****************************************************************************
 * drivers/clk/song/song-clk.c
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

#include <nuttx/config.h>

#include <nuttx/clk/clk.h>
#include <nuttx/clk/clk-provider.h>
#include <nuttx/clk/song/song-clk.h>
#include <stdio.h>

#include "song-clk.h"

#ifdef CONFIG_SONG_CLK

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static struct clk *song_clk_register_gate(const char *name,
                    const char *parent_name, uint64_t flags,
                    uint32_t reg_base, uint32_t en_offset,
                    uint8_t bit_idx, uint64_t private_flags);
static struct clk *song_clk_register_mux_sdiv(const char *name,
                    const char **parent_name, uint8_t num_parents,
                    uint64_t flags, uint32_t reg_base,
                    uint32_t en_offset, uint8_t en_shift,
                    uint32_t mux_offset, uint8_t mux_shift, uint8_t mux_width,
                    uint32_t div_offset, uint8_t div_shift, uint8_t div_width,
                    uint64_t private_flags);
static struct clk *song_clk_register_gr(const char *name,
                    const char *parent_name, uint64_t flags,
                    uint32_t reg_base, uint32_t en_offset, uint8_t en_shift,
                    uint32_t mul_offset, uint8_t mul_shift, uint8_t mul_width,
                    uint64_t private_flags);
static struct clk *song_clk_register_gr_fdiv(const char *name,
                    const char *parent_name, uint64_t flags,
                    uint32_t reg_base, uint32_t en_offset,
                    uint8_t en_shift, uint32_t gr_offset,
                    uint32_t div_offset, uint32_t fixed_gr,
                    uint64_t private_flags);
static struct clk *song_clk_register_sdiv(const char *name,
                    const char *parent_name, uint64_t flags,
                    uint32_t reg_base, uint32_t en_offset, uint8_t en_shift,
                    uint32_t div_offset, uint8_t div_shift, uint8_t div_width,
                    uint64_t private_flags);
static struct clk *song_clk_register_sdiv_gr(const char *name,
                    const char *parent_name, uint64_t flags,
                    uint32_t reg_base, uint32_t div_offset,
                    uint64_t private_flags);
static struct clk *song_clk_register_sdiv_sdiv(const char *name,
                    const char *parent_name, uint64_t flags,
                    uint32_t reg_base, uint32_t div_offset,
                    uint64_t private_flags);
static struct clk *song_clk_register_gr_sdiv(const char *name,
                    const char *parent_name, uint64_t flags,
                    uint32_t reg_base, uint32_t div_offset,
                    uint64_t private_flags);
static struct clk *song_clk_register_mux_sdiv_gr(const char *name,
                    const char **parent_name, uint8_t num_parents,
                    uint64_t flags, uint32_t reg_base,
                    uint32_t div_offset, uint8_t div_width, uint8_t mux_width,
                    uint64_t private_flags);

static int song_register_fixed_rate_clks(
                    const struct song_fixed_rate_clk *fixed_rate_clks);
static int song_register_fixed_factor_clks(
                    const struct song_fixed_factor_clk *fixed_factor_clks);
static int song_register_gate_clks(uint32_t reg_base,
                    const struct song_gate_clk *gate_clks);
static int song_register_sdiv_clks(uint32_t reg_base,
                    const struct song_sdiv_clk *sdiv_clks);
static int song_register_gr_clks(uint32_t reg_base,
                    const struct song_gr_clk *gr_clks);
static int song_register_sdiv_sdiv_clks(uint32_t reg_base,
                    const struct song_sdiv_sdiv_clk *sdiv_sdiv_clks);
static int song_register_gr_fdiv_clks(uint32_t reg_base,
                    const struct song_gr_fdiv_clk *gr_fdiv_clks);
static int song_register_sdiv_gr_clks(uint32_t reg_base,
                    const struct song_sdiv_gr_clk *sdiv_gr_clks);
static int song_register_gr_sdiv_clks(uint32_t reg_base,
                    const struct song_sdiv_gr_clk *gr_sdiv_clks);
static int song_register_mux_sdiv_clks(uint32_t reg_base,
                    const struct song_mux_sdiv_clk *mux_sdiv_clks);
static int song_register_mux_gate_clks(uint32_t reg_base,
                    const struct song_mux_gate_clk *mux_gate_clks);
static int song_register_phase_clks(uint32_t reg_base,
                    const struct song_phase_clk *phase_clks);
static int song_register_mux_sdiv_gr_clks(uint32_t reg_base,
                    const struct song_mux_sdiv_gr_clk *mux_sdiv_gr_clks);
static int song_register_pll_clks(uint32_t reg_base,
                    const struct song_pll_clk *pll_clks);
#ifdef CONFIG_CLK_RPMSG
static int song_register_rpmsg_clks(uint32_t reg_base,
                    const struct song_rpmsg_clk *rpmsg_clks);
#endif

static int song_set_default_rate(const struct song_default_rate_clk *def_rate);

/************************************************************************************
 * Private Functions
 ************************************************************************************/

static struct clk *song_clk_register_gate(const char *name,
    const char *parent_name, uint64_t flags,
    uint32_t reg_base, uint32_t en_offset,
    uint8_t bit_idx, uint64_t private_flags)
{
  uint8_t clk_gate_flags = (private_flags >> SONG_CLK_GATE_FLAG_SHIFT) &
    SONG_CLK_FLAG_MASK;

  return clk_register_gate(name, parent_name, flags,
    reg_base + en_offset, bit_idx, clk_gate_flags);
}

static struct clk *song_clk_register_mux_sdiv(const char *name,
    const char **parent_name, uint8_t num_parents,
    uint64_t flags, uint32_t reg_base,
    uint32_t en_offset, uint8_t en_shift,
    uint32_t mux_offset, uint8_t mux_shift, uint8_t mux_width,
    uint32_t div_offset, uint8_t div_shift, uint8_t div_width,
    uint64_t private_flags)
{
  struct clk *clk = NULL;
  char mux_clk[SONG_CLK_NAME_MAX], gate_clk[SONG_CLK_NAME_MAX];
  uint8_t gate_flags, mux_flags;
  uint16_t div_flags;
  const char *pname = *parent_name, *cname = gate_clk;

  snprintf(gate_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "gate");
  snprintf(mux_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "mux");

  if (mux_offset)
    {
      mux_flags = (private_flags >> SONG_CLK_MUX_FLAG_SHIFT) &
        SONG_CLK_FLAG_MASK;
      mux_flags |= CLK_MUX_ROUND_CLOSEST;

      clk = clk_register_mux(mux_clk, parent_name, num_parents, flags,
        reg_base + mux_offset, mux_shift, MASK(mux_width), mux_flags);
      if (!clk)
        return clk;

      pname = mux_clk;
    }

  if (en_offset)
    {
      if (!div_offset)
        cname = name;

      gate_flags = (private_flags >> SONG_CLK_GATE_FLAG_SHIFT) &
        SONG_CLK_FLAG_MASK;

      clk = clk_register_gate(cname, pname, flags,
        reg_base + en_offset, en_shift, gate_flags);
      if (!clk)
        return clk;

      pname = gate_clk;
    }

  if (div_offset)
    {
      div_flags = (private_flags >> SONG_CLK_DIV_FLAG_SHIFT) &
        SONG_CLK_DIV_MASK;
      div_flags |= CLK_DIVIDER_ROUND_CLOSEST;

      return clk_register_divider(name, pname, flags,
        reg_base + div_offset, div_shift, div_width,
        div_flags);
    }

  return clk;
}

static struct clk *song_clk_register_gr(const char *name,
    const char *parent_name, uint64_t flags,
    uint32_t reg_base, uint32_t en_offset, uint8_t en_shift,
    uint32_t mul_offset, uint8_t mul_shift, uint8_t mul_width,
    uint64_t private_flags)
{
  struct clk *clk;
  uint8_t mult_flags, song_flags, gate_flags;
  char gate_clk[SONG_CLK_NAME_MAX], mult_clk[SONG_CLK_NAME_MAX];
  uint32_t fixed_div = 8;
  const char *pname = parent_name;
  flags |= CLK_SET_RATE_PARENT;

  snprintf(gate_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "gate");
  snprintf(mult_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "mult");

  mult_flags = (private_flags >> SONG_CLK_MULT_FLAG_SHIFT) &
    SONG_CLK_FLAG_MASK;

  song_flags = (private_flags >> SONG_CLK_PRIVATE_FLAG_SHIFT) &
    SONG_CLK_FLAG_MASK;

  if (song_flags & SONG_CLK_GR_DIV_16)
    fixed_div = 16;

  if (en_offset)
    {
      gate_flags = (private_flags >> SONG_CLK_GATE_FLAG_SHIFT) &
       SONG_CLK_FLAG_MASK;

      clk = clk_register_gate(gate_clk, pname, flags,
        reg_base + en_offset, en_shift, gate_flags);
      if (!clk)
        return clk;

      pname = gate_clk;
    }

  mult_flags |= CLK_MULT_ROUND_CLOSEST;
  clk = clk_register_multiplier(mult_clk, pname,
    flags, reg_base + mul_offset, mul_shift,
    mul_width, mult_flags);
  if (!clk)
    return clk;

  return clk_register_fixed_factor(name, mult_clk, flags,
    1, fixed_div);
}

static struct clk *song_clk_register_gr_fdiv(const char *name,
    const char *parent_name, uint64_t flags,
    uint32_t reg_base, uint32_t en_offset,
    uint8_t en_shift, uint32_t gr_offset,
    uint32_t div_offset, uint32_t fixed_gr,
    uint64_t private_flags)
{
  struct clk *clk;
  char gr_clk[SONG_CLK_NAME_MAX], fixed_clk[SONG_CLK_NAME_MAX];
  uint8_t frac_flags;
  const char *pname = parent_name;

  snprintf(gr_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "gr_fdiv");
  snprintf(fixed_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "fixed");

  frac_flags = (private_flags >> SONG_CLK_FRAC_FLAG_SHIFT) &
    SONG_CLK_FLAG_MASK;

  if (gr_offset)
    {
      clk = song_clk_register_gr(gr_clk, pname, flags, reg_base, gr_offset,
        SONG_CLK_GR_FDIV_EN_SHIFT, gr_offset, SONG_CLK_GR_FDIV_GR_SHIFT,
        SONG_CLK_GR_FDIV_GR_WIDTH, private_flags);
      if (!clk)
        return clk;

      pname = gr_clk;
    }

  clk = clk_register_fixed_factor(fixed_clk, pname, flags, 1, 2);
  if (!clk)
    return clk;

  return clk_register_fractional_divider(name, fixed_clk, flags,
    reg_base + div_offset, SONG_CLK_GR_FDIV_MUL_SHIFT,
    SONG_CLK_GR_FDIV_MUL_WIDTH, SONG_CLK_GR_FDIV_DIV_SHIFT,
    SONG_CLK_GR_FDIV_DIV_WIDTH, frac_flags);
}

static struct clk *song_clk_register_sdiv(const char *name,
    const char *parent_name, uint64_t flags,
    uint32_t reg_base, uint32_t en_offset, uint8_t en_shift,
    uint32_t div_offset, uint8_t div_shift, uint8_t div_width,
    uint64_t private_flags)
{
  return song_clk_register_mux_sdiv(name, &parent_name, 1, flags,
    reg_base, en_offset, en_shift, 0, 0, 0, div_offset,
    div_shift, div_width, private_flags);
}

static struct clk *song_clk_register_sdiv_gr(const char *name,
    const char *parent_name, uint64_t flags,
    uint32_t reg_base, uint32_t div_offset,
    uint64_t private_flags)
{
  struct clk *clk;
  char mid_clk[SONG_CLK_NAME_MAX];

  snprintf(mid_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "sdiv_gr");

  clk = song_clk_register_sdiv(mid_clk, parent_name,
    flags, reg_base, div_offset,
    SONG_CLK_SDIV_GR_EN_SHIFT, div_offset, SONG_CLK_SDIV_GR_DIV_SHIFT,
    SONG_CLK_SDIV_GR_DIV_WIDTH, private_flags);

  if (!clk)
    return NULL;

  flags |= CLK_SET_RATE_PARENT;
  clk = song_clk_register_gr(name, mid_clk, flags, reg_base, 0, 0,
    div_offset, SONG_CLK_SDIV_GR_GR_SHIFT, SONG_CLK_SDIV_GR_GR_WIDTH,
    private_flags);

  return clk;
}

static struct clk *song_clk_register_sdiv_sdiv(const char *name,
    const char *parent_name, uint64_t flags,
    uint32_t reg_base, uint32_t div_offset,
    uint64_t private_flags)
{
  struct clk *clk;
  char mid_clk[SONG_CLK_NAME_MAX];

  snprintf(mid_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "sdiv_sdiv");

  clk = song_clk_register_sdiv(mid_clk, parent_name,
    flags, reg_base, div_offset,
    SONG_CLK_SDIV_SDIV_EN_SHIFT, div_offset, SONG_CLK_SDIV_SDIV_DIV0_SHIFT,
    SONG_CLK_SDIV_SDIV_DIV0_WIDTH, private_flags);

  if (!clk)
    return NULL;

  flags |= CLK_SET_RATE_PARENT;
  clk = song_clk_register_sdiv(name, mid_clk, flags, reg_base, 0, 0,
    div_offset, SONG_CLK_SDIV_SDIV_DIV1_SHIFT,
    SONG_CLK_SDIV_SDIV_DIV1_WIDTH, private_flags);

  return clk;
}

static struct clk *song_clk_register_gr_sdiv(const char *name,
    const char *parent_name, uint64_t flags,
    uint32_t reg_base, uint32_t div_offset,
    uint64_t private_flags)
{
  struct clk *clk;
  char mid_clk[SONG_CLK_NAME_MAX];

  snprintf(mid_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "gr_sdiv");

  clk = song_clk_register_gr(mid_clk, parent_name,
    flags, reg_base, div_offset,
    SONG_CLK_GR_SDIV_EN_SHIFT, div_offset, SONG_CLK_GR_SDIV_GR_SHIFT,
    SONG_CLK_GR_SDIV_GR_WIDTH, private_flags);

  if (!clk)
    return NULL;

  flags |= CLK_SET_RATE_PARENT;
  clk = song_clk_register_sdiv(name, mid_clk, flags, reg_base, 0, 0,
    div_offset, SONG_CLK_GR_SDIV_DIV_SHIFT,
    SONG_CLK_GR_SDIV_DIV_WIDTH, private_flags);

  return clk;
}

static struct clk *song_clk_register_mux_sdiv_gr(const char *name,
    const char **parent_name, uint8_t num_parents,
    uint64_t flags, uint32_t reg_base,
    uint32_t div_offset, uint8_t div_width, uint8_t mux_width,
    uint64_t private_flags)
{
  struct clk *clk;
  char mid_clk[SONG_CLK_NAME_MAX];

  snprintf(mid_clk, SONG_CLK_NAME_MAX, "%s_%s", name, "sdiv_gr");

  clk = song_clk_register_mux_sdiv(mid_clk, parent_name, num_parents,
    flags, reg_base, div_offset, SONG_CLK_MUX_SDIV_GR_EN_SHIFT,
    div_offset, SONG_CLK_MUX_SDIV_GR_MUX_SHIFT, mux_width,
    div_offset, SONG_CLK_MUX_SDIV_GR_DIV_SHIFT, div_width, private_flags);

  if (!clk)
    return NULL;

  flags |= CLK_SET_RATE_PARENT;
  clk = song_clk_register_gr(name, mid_clk, flags, reg_base, 0, 0,
    div_offset, SONG_CLK_MUX_SDIV_GR_GR_SHIFT, SONG_CLK_MUX_SDIV_GR_GR_WIDTH, private_flags);

  return clk;
}

static int song_register_fixed_rate_clks(
              const struct song_fixed_rate_clk *fixed_rate_clks)
{
  struct clk *clk = NULL;

  while (fixed_rate_clks->name)
    {
      clk = clk_register_fixed_rate(
              fixed_rate_clks->name,
              NULL,
              fixed_rate_clks->flags,
              fixed_rate_clks->fixed_rate);

      if (!clk)
        {
          return -EINVAL;
        }
      fixed_rate_clks++;
    }

  return 0;
}

static int song_register_fixed_factor_clks(
            const struct song_fixed_factor_clk *fixed_factor_clks)
{
  struct clk *clk = NULL;

  while (fixed_factor_clks->name)
    {
      clk = clk_register_fixed_factor(
              fixed_factor_clks->name,
              fixed_factor_clks->parent_name,
              fixed_factor_clks->flags,
              fixed_factor_clks->fixed_mul,
              fixed_factor_clks->fixed_div);

      if (!clk)
        {
          return -EINVAL;
        }

      fixed_factor_clks++;
    }

  return 0;
}

static int song_register_gate_clks(uint32_t reg_base,
            const struct song_gate_clk *gate_clks)
{
  struct clk *clk = NULL;

  while (gate_clks->name)
    {
      clk = song_clk_register_gate(
            gate_clks->name,
            gate_clks->parent_name,
            gate_clks->flags,
            reg_base,
            gate_clks->en_offset,
            gate_clks->en_shift,
            gate_clks->private_flags);

      if (!clk)
        {
          return -EINVAL;
        }

      gate_clks++;
    }

  return 0;
}

#ifdef CONFIG_CLK_RPMSG
static int song_register_rpmsg_clks(uint32_t reg_base,
            const struct song_rpmsg_clk *rpmsg_clks)
{
  struct clk *clk = NULL;

  while (rpmsg_clks->name)
    {
      clk = clk_register_rpmsg(
            rpmsg_clks->name,
            rpmsg_clks->flags);
      if (!clk)
        {
          return -EINVAL;
        }

      rpmsg_clks++;
    }

  return 0;
}
#endif

static int song_register_sdiv_clks(uint32_t reg_base,
            const struct song_sdiv_clk *sdiv_clks)
{
  struct clk *clk = NULL;

  while (sdiv_clks->name)
    {
      clk = song_clk_register_sdiv(sdiv_clks->name,
              sdiv_clks->parent_name,
              sdiv_clks->flags,
              reg_base,
              sdiv_clks->en_offset,
              sdiv_clks->en_shift,
              sdiv_clks->div_offset,
              sdiv_clks->div_shift,
              sdiv_clks->div_width,
              sdiv_clks->private_flags);

      if (!clk)
        {
          return -EINVAL;
        }

      sdiv_clks++;
    }

  return 0;
}

static int song_register_gr_clks(uint32_t reg_base,
            const struct song_gr_clk *gr_clks)
{
  struct clk *clk = NULL;

  while (gr_clks->name)
    {
      clk = song_clk_register_gr(gr_clks->name,
              gr_clks->parent_name,
              gr_clks->flags,
              reg_base,
              gr_clks->en_offset,
              gr_clks->en_shift,
              gr_clks->mul_offset,
              gr_clks->mul_shift,
              gr_clks->mul_width,
              gr_clks->private_flags);
      if (!clk)
        {
          return -EINVAL;
        }

        gr_clks++;
      }

  return 0;
}

static int song_register_sdiv_sdiv_clks(uint32_t reg_base,
            const struct song_sdiv_sdiv_clk *sdiv_sdiv_clks)
{
  struct clk *clk = NULL;

  while (sdiv_sdiv_clks->name)
    {
      clk = song_clk_register_sdiv_sdiv(
              sdiv_sdiv_clks->name,
              sdiv_sdiv_clks->parent_name,
              sdiv_sdiv_clks->flags,
              reg_base,
              sdiv_sdiv_clks->div_offset,
              sdiv_sdiv_clks->private_flags);
      if (!clk)
        {
          return -EINVAL;
        }

      sdiv_sdiv_clks++;
    }

  return 0;
}

static int song_register_gr_fdiv_clks(uint32_t reg_base,
            const struct song_gr_fdiv_clk *gr_fdiv_clks)
{
  struct clk *clk = NULL;

  while (gr_fdiv_clks->name)
    {
      clk = song_clk_register_gr_fdiv(
              gr_fdiv_clks->name,
              gr_fdiv_clks->parent_name,
              gr_fdiv_clks->flags,
              reg_base,
              gr_fdiv_clks->en_offset,
              gr_fdiv_clks->en_shift,
              gr_fdiv_clks->gr_offset,
              gr_fdiv_clks->div_offset,
              gr_fdiv_clks->fixed_gr,
              gr_fdiv_clks->private_flags);
      if (!clk)
        {
          return -EINVAL;
        }

      gr_fdiv_clks++;
    }

  return 0;
}

static int song_register_sdiv_gr_clks(uint32_t reg_base,
            const struct song_sdiv_gr_clk *sdiv_gr_clks)
{
  struct clk *clk = NULL;

  while (sdiv_gr_clks->name)
    {
      clk = song_clk_register_sdiv_gr(sdiv_gr_clks->name,
              sdiv_gr_clks->parent_name,
              sdiv_gr_clks->flags,
              reg_base,
              sdiv_gr_clks->div_offset,
              sdiv_gr_clks->private_flags);
      if (!clk)
        {
          return -EINVAL;
        }

      sdiv_gr_clks++;
    }

  return 0;
}

static int song_register_gr_sdiv_clks(uint32_t reg_base,
            const struct song_sdiv_gr_clk *gr_sdiv_clks)
{
  struct clk *clk = NULL;

  while (gr_sdiv_clks->name)
    {
      clk = song_clk_register_gr_sdiv(
              gr_sdiv_clks->name,
              gr_sdiv_clks->parent_name,
              gr_sdiv_clks->flags,
              reg_base,
              gr_sdiv_clks->div_offset,
              gr_sdiv_clks->private_flags);
      if (!clk)
        {
          return -EINVAL;
        }

      gr_sdiv_clks++;
    }

  return 0;
}

static int song_register_mux_sdiv_clks(uint32_t reg_base,
            const struct song_mux_sdiv_clk *mux_sdiv_clks)
{
  struct clk *clk = NULL;

  while (mux_sdiv_clks->name)
    {
      clk = song_clk_register_mux_sdiv(
              mux_sdiv_clks->name,
              mux_sdiv_clks->parent_name,
              mux_sdiv_clks->num_parents,
              mux_sdiv_clks->flags,
              reg_base,
              0,
              0,
              mux_sdiv_clks->mux_offset,
              mux_sdiv_clks->mux_shift,
              mux_sdiv_clks->mux_width,
              mux_sdiv_clks->div_offset,
              mux_sdiv_clks->div_shift,
              mux_sdiv_clks->div_width,
              mux_sdiv_clks->private_flags);
      if (!clk)
        {
          return -EINVAL;
        }

      mux_sdiv_clks++;
    }

  return 0;
}

static int song_register_mux_gate_clks(uint32_t reg_base,
            const struct song_mux_gate_clk *mux_gate_clks)
{
  struct clk *clk = NULL;

  while (mux_gate_clks->name)
    {
      clk = song_clk_register_mux_sdiv(
              mux_gate_clks->name,
              mux_gate_clks->parent_name,
              mux_gate_clks->num_parents,
              mux_gate_clks->flags,
              reg_base,
              mux_gate_clks->en_offset,
              mux_gate_clks->en_shift,
              mux_gate_clks->mux_offset,
              mux_gate_clks->mux_shift,
              mux_gate_clks->mux_width,
              0,
              0,
              0,
              mux_gate_clks->private_flags);
      if (!clk)
        {
          return -EINVAL;
        }

      mux_gate_clks++;
    }

  return 0;
}

static int song_register_phase_clks(uint32_t reg_base,
            const struct song_phase_clk *phase_clks)
{
  struct clk *clk = NULL;

  while (phase_clks->name)
    {
      clk = clk_register_phase(phase_clks->name,
              phase_clks->parent_name,
              phase_clks->flags,
              reg_base + phase_clks->reg_offset,
              phase_clks->phase_shift,
              phase_clks->phase_width,
              phase_clks->phase_flags);
      if (!clk)
        {
          return -EINVAL;
        }

      phase_clks++;
    }

  return 0;
}

static int song_register_mux_sdiv_gr_clks(uint32_t reg_base,
            const struct song_mux_sdiv_gr_clk *mux_sdiv_gr_clks)
{
  struct clk *clk = NULL;

  while (mux_sdiv_gr_clks->name)
    {
      clk = song_clk_register_mux_sdiv_gr(
              mux_sdiv_gr_clks->name,
              mux_sdiv_gr_clks->parent_name,
              mux_sdiv_gr_clks->num_parents,
              mux_sdiv_gr_clks->flags,
              reg_base,
              mux_sdiv_gr_clks->div_offset,
              mux_sdiv_gr_clks->div_width,
              mux_sdiv_gr_clks->mux_width,
              mux_sdiv_gr_clks->private_flags);
      if (!clk)
        {
          return -EINVAL;
        }

      mux_sdiv_gr_clks++;
    }

  return 0;
}

static int song_register_pll_clks(uint32_t reg_base,
            const struct song_pll_clk *pll_clks)
{
  struct clk *clk = NULL;

  while (pll_clks->name)
    {
      clk = clk_register_pll(
              pll_clks->name,
              pll_clks->parent_name,
              pll_clks->flags,
              reg_base + pll_clks->cfg_reg0_offset,
              reg_base + pll_clks->cfg_reg1_offset,
              reg_base + pll_clks->ctl_reg_offset,
              pll_clks->ctl_shift);
      if (!clk)
        {
          return -EINVAL;
        }

      pll_clks++;
    }

  return 0;
}

static int song_set_default_rate(const struct song_default_rate_clk *def_rate)
{
  struct clk *clk = NULL;
  int ret = 0;

  while (def_rate->name)
    {
      clk = clk_get(def_rate->name);
      if (!clk)
        return -EINVAL;
      ret = clk_set_rate(clk, def_rate->rate);
      if (ret)
        return ret;

      def_rate ++;
    }

  return 0;
}

/****************************************************************************
   * Public Functions
****************************************************************************/

/****************************************************************************
 * Name: song_clk_initialize
 *
 * Description:
 *   Create a set of clk driver instances for the song platform.
 *   This will initialize the clk follow the configuration table.
 *
 * Input Parameters:
 *   base      - the clk controller base address.
 *   table     - the song platform clk table that to be configurated.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int song_clk_initialize(uint32_t base, const struct song_clk_table *table)
{
  int ret = 0;

  if (table->fixed_rate_clks)
    {
      ret = song_register_fixed_rate_clks(table->fixed_rate_clks);
      if (ret)
        return ret;
    }

  if (table->fixed_factor_clks)
    {
      ret = song_register_fixed_factor_clks(table->fixed_factor_clks);
      if (ret)
        return ret;
    }

  if (table->pll_clks)
    {
      ret = song_register_pll_clks(base, table->pll_clks);
      if (ret)
        return ret;
    }

  if (table->phase_clks)
    {
      ret = song_register_phase_clks(base, table->phase_clks);
      if (ret)
        return ret;
    }

  if (table->sdiv_clks)
    {
      ret = song_register_sdiv_clks(base, table->sdiv_clks);
      if (ret)
        return ret;
    }

  if (table->gate_clks)
    {
      ret = song_register_gate_clks(base, table->gate_clks);
      if (ret)
        return ret;
    }

  if (table->gr_clks)
    {
      ret = song_register_gr_clks(base, table->gr_clks);
      if (ret)
        return ret;
    }

  if (table->sdiv_sdiv_clks)
    {
      ret = song_register_sdiv_sdiv_clks(base, table->sdiv_sdiv_clks);
      if (ret)
        return ret;
    }

  if (table->gr_fdiv_clks)
    {
      ret = song_register_gr_fdiv_clks(base, table->gr_fdiv_clks);
      if (ret)
        return ret;
    }

  if (table->mux_sdiv_clks)
    {
      ret = song_register_mux_sdiv_clks(base, table->mux_sdiv_clks);
      if (ret)
        return ret;
    }

  if (table->mux_gate_clks)
    {
      ret = song_register_mux_gate_clks(base, table->mux_gate_clks);
      if (ret)
        return ret;
    }

  if (table->sdiv_gr_clks)
    {
      ret = song_register_sdiv_gr_clks(base, table->sdiv_gr_clks);
      if (ret)
        return ret;
    }

  if (table->mux_sdiv_gr_clks)
    {
      ret = song_register_mux_sdiv_gr_clks(base, table->mux_sdiv_gr_clks);
      if (ret)
        return ret;
    }

  if (table->gr_sdiv_clks)
    {
      ret = song_register_gr_sdiv_clks(base, table->gr_sdiv_clks);
      if (ret)
        return ret;
    }

#ifdef CONFIG_CLK_RPMSG
  if (table->rpmsg_clks)
    {
      ret = song_register_rpmsg_clks(base, table->rpmsg_clks);
      if (ret)
        return ret;
    }
#endif

  if (table->def_rate)
    {
      ret = song_set_default_rate(table->def_rate);
      if (ret)
        return ret;
    }

  return 0;
}

#endif /* CONFIG_SONG_CLK */