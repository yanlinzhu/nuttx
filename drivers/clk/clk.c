/****************************************************************************
 * drivers/clk/clk.c
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
#include <nuttx/list.h>
#include <nuttx/mutex.h>

#include <debug.h>

#ifdef CONFIG_CLK

/****************************************************************************
 * Private Datas
 ****************************************************************************/
static mutex_t g_clk_lock  = MUTEX_INITIALIZER;

static struct list_node g_clk_root_list   = LIST_INITIAL_VALUE(g_clk_root_list);
static struct list_node g_clk_orphan_list = LIST_INITIAL_VALUE(g_clk_orphan_list);

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void clk_lock(void);
static void clk_unlock(void);

static int clk_fetch_parent_index(struct clk *clk, struct clk *parent);
static void clk_init_parent(struct clk *clk);
static void clk_reparent(struct clk *clk, struct clk *parent);
static void __clk_reparent(struct clk *clk, struct clk *parent);

static uint64_t clk_recalc(struct clk *clk, uint64_t parent_rate);
static void __clk_recalc_rate(struct clk *clk);

static void clk_calc_subtree(struct clk *clk, uint64_t new_rate,
              struct clk *new_parent, uint8_t p_index);
static struct clk *clk_calc_new_rates(struct clk *clk, uint64_t rate);
static void clk_change_rate(struct clk *clk, uint64_t best_parent_rate);

static uint64_t __clk_get_rate(struct clk *clk);
static uint64_t __clk_round_rate(struct clk *clk, uint64_t rate);
static int __clk_enable(struct clk *clk);
static void __clk_disable(struct clk *clk);

static struct clk *__clk_lookup(const char *name, struct clk *clk);
static int __clk_register(struct clk *clk);

/****************************************************************************
 * Private Function
 ****************************************************************************/
static void clk_lock(void)
{
  nxmutex_lock(&g_clk_lock);
}

static void clk_unlock(void)
{
  nxmutex_unlock(&g_clk_lock);
}

static int clk_fetch_parent_index(struct clk *clk, struct clk *parent)
{
  int i;

  if (!parent)
    {
      return -EINVAL;
    }

  for (i = 0; i < clk->num_parents; i++)
    {
      if (clk->parents[i] == parent)
        return i;

      if (!strcmp(clk->parent_names[i], parent->name))
        {
          clk->parents[i] = clk_get(parent->name);
          return i;
        }
    }

  return -EINVAL;
}

static void clk_reparent(struct clk *clk, struct clk *parent)
{
  list_delete(&clk->child_node);

  if (parent)
    {
      if (parent->new_child == clk)
        parent->new_child = NULL;
      list_add_head(&parent->children, &clk->child_node);
    }

  clk->parent = parent;
}

static void __clk_reparent(struct clk *clk, struct clk *parent)
{
  clk_reparent(clk, parent);
  __clk_recalc_rate(clk);
}

static uint64_t clk_recalc(struct clk *clk, uint64_t parent_rate)
{
  if (clk->ops->recalc_rate)
    return clk->ops->recalc_rate(clk, parent_rate);
  return parent_rate;
}

static void __clk_recalc_rate(struct clk *clk)
{
  uint64_t parent_rate = 0;
  struct clk *child;

  if (clk->parent)
    parent_rate = clk->parent->rate;

  clk->rate = clk_recalc(clk, parent_rate);

  list_for_every_entry(&clk->children, child, struct clk, child_node)
    __clk_recalc_rate(child);
}

static void clk_calc_subtree(struct clk *clk, uint64_t new_rate,
              struct clk *new_parent, uint8_t p_index)
{
  struct clk *child;

  clk->new_rate = new_rate;
  clk->new_parent = new_parent;
  clk->new_parent_index = p_index;

  clk->new_child = NULL;
  if (new_parent && new_parent != clk->parent)
    new_parent->new_child = clk;

  list_for_every_entry(&clk->children, child, struct clk, child_node)
    {
      child->new_rate = clk_recalc(child, new_rate);
      clk_calc_subtree(child, child->new_rate, NULL, 0);
    }
}

static struct clk *clk_calc_new_rates(struct clk *clk, uint64_t rate)
{
  struct clk *top = clk;
  struct clk *old_parent, *parent;
  uint64_t best_parent_rate = 0;
  uint64_t new_rate = 0;
  int p_index = 0;

  if (!clk)
    return NULL;

  parent = old_parent = clk->parent;
  if (parent)
    best_parent_rate = parent->rate;

  if (clk->ops->determine_rate)
    {
      new_rate = clk->ops->determine_rate(clk, rate, &best_parent_rate,
                  &parent);
    }
  else if (clk->ops->round_rate)
    {
      new_rate = clk->ops->round_rate(clk, rate, &best_parent_rate);
    }
  else if (!parent || !(clk->flags & CLK_SET_RATE_PARENT))
    {
      clk->new_rate = clk->rate;
      return NULL;
    }
  else
    {
      top = clk_calc_new_rates(parent, rate);
      new_rate = parent->new_rate;
      goto out;
    }

  if (parent)
    {
      p_index = clk_fetch_parent_index(clk, parent);
      if (p_index < 0)
        {
          clkerr("clk %s can not be parent of clk %s\n",
           parent->name, clk->name);
          return NULL;
        }
    }

  if ((clk->flags & CLK_SET_RATE_PARENT) && parent &&
    best_parent_rate != parent->rate)
    top = clk_calc_new_rates(parent, best_parent_rate);

out:
  clk_calc_subtree(clk, new_rate, parent, p_index);
  return top;
}

static void clk_change_rate(struct clk *clk, uint64_t best_parent_rate)
{
  struct clk *child;
  bool skip_set_rate = false;
  struct clk *old_parent;

  list_for_every_entry(&clk->children, child, struct clk, child_node)
    {
      if (child->new_parent && child->new_parent != clk)
        continue;
      if (child->new_rate > child->rate)
        clk_change_rate(child, clk->new_rate);
    }

  old_parent = clk->parent;

  if (clk->new_parent && clk->new_parent != clk->parent)
    {
      if (clk->enable_count)
        {
          clk_enable(clk->new_parent);
          clk_enable(clk);
        }

      clk_reparent(clk, clk->new_parent);

      if (clk->ops->set_rate_and_parent)
        {
          skip_set_rate = true;
          clk->ops->set_rate_and_parent(clk, clk->new_rate, best_parent_rate,
                    clk->new_parent_index);
        }
      else if (clk->ops->set_parent)
        {
          clk->ops->set_parent(clk, clk->new_parent_index);
        }

      if (clk->enable_count)
        {
          clk_disable(clk);
          clk_disable(old_parent);
        }
    }

  if (!skip_set_rate && clk->ops->set_rate)
    clk->ops->set_rate(clk, clk->new_rate, best_parent_rate);

  clk->rate = clk->new_rate;

  list_for_every_entry(&clk->children, child, struct clk, child_node)
    {
      if (child->new_parent && child->new_parent != clk)
        continue;
      if (child->new_rate != child->rate)
        clk_change_rate(child, clk->new_rate);
    }

  if (clk->new_child && clk->new_child->new_rate != clk->new_child->rate)
    clk_change_rate(clk->new_child, clk->new_rate);
}

static struct clk *__clk_lookup(const char *name, struct clk *clk)
{
  struct clk *child;
  struct clk *ret;

  if (!strcmp(clk->name, name))
    return clk;

  list_for_every_entry(&clk->children, child, struct clk, child_node)
    {
      ret = __clk_lookup(name, child);
      if (ret)
        return ret;
    }

  return NULL;
}

static uint64_t __clk_get_rate(struct clk *clk)
{
  if (!clk)
      return 0;

  return clk->rate;
}

static uint64_t __clk_round_rate(struct clk *clk, uint64_t rate)
{
  uint64_t parent_rate = 0;
  struct clk *parent;

  if (!clk)
    return 0;

  parent = clk->parent;
  if (parent)
    parent_rate = parent->rate;

  if (clk->ops->determine_rate)
    return clk->ops->determine_rate(clk, rate, &parent_rate,
               &parent);
  else if (clk->ops->round_rate)
    return clk->ops->round_rate(clk, rate, &parent_rate);
  else if (clk->flags & CLK_SET_RATE_PARENT)
    return __clk_round_rate(clk->parent, rate);
  else
    return clk->rate;
}

static int __clk_enable(struct clk *clk)
{
  int ret = 0;

  if (!clk)
    return 0;

  if (clk->enable_count == 0)
    {
      ret = __clk_enable(clk->parent);
      if (ret)
        return ret;

      if (clk->ops->enable)
        {
          ret = clk->ops->enable(clk);
          if (ret)
            {
              __clk_disable(clk->parent);
              return ret;
            }
        }
    }

  clk->enable_count++;
  return ret;
}

static void __clk_disable(struct clk *clk)
{

  if (!clk)
    return;
  if (clk->enable_count == 0)
    return;
  if (--clk->enable_count > 0)
    return;
  if (clk->ops->disable)
    clk->ops->disable(clk);

  if (clk->parent)
    __clk_disable(clk->parent);
}

static void clk_init_parent(struct clk *clk)
{
  uint8_t  index;

  if (!clk->num_parents)
    return;

  if (clk->num_parents == 1)
    {
      clk->parent = clk_get(clk->parent_names[0]);
      return;
    }

  if (!clk->ops->get_parent)
    {
      clkerr("multi-parent clks must implement .get_parent\n");
      return;
    };

  index = clk->ops->get_parent(clk);

  clk->parent = clk_get_parent_by_index(clk, index);
}

static int __clk_register(struct clk *clk)
{
  int i, ret = 0;
  struct clk *orphan;
  struct clk *temp_orphan;

  if (!clk)
    return -EINVAL;

  clk_lock();

  if (clk_get(clk->name))
    {
      clkwarn("clk %s already initialized\n", clk->name);
      ret = -EEXIST;
      goto out;
    }

  if (clk->ops->set_rate &&
    !((clk->ops->round_rate || clk->ops->determine_rate) &&
        clk->ops->recalc_rate))
    {
      clkerr("%s must implement .round_rate or .determine_rate in addition to .recalc_rate\n",
          clk->name);
      ret = -EINVAL;
      goto out;
    }

  if (clk->ops->set_parent && !clk->ops->get_parent)
    {
      clkerr("%s must implement .get_parent & .set_parent\n",
          clk->name);
      ret = -EINVAL;
      goto out;
    }

  if (clk->ops->set_rate_and_parent &&
      !(clk->ops->set_parent && clk->ops->set_rate))
    {
      clkerr("%s must implement .set_parent & .set_rate\n",
          clk->name);
      ret = -EINVAL;
      goto out;
    }

  clk_init_parent(clk);

  if (clk->parent)
    {
      list_add_head(&clk->parent->children, &clk->child_node);
    }
  else if (clk->flags & CLK_IS_ROOT)
    {
      list_add_head(&g_clk_root_list, &clk->child_node);
    }
  else
    {
      list_add_head(&g_clk_orphan_list, &clk->child_node);
    }

  if (clk->ops->get_phase)
    clk->phase = clk->ops->get_phase(clk);

  if (clk->ops->recalc_rate)
    clk->rate = clk->ops->recalc_rate(clk,
      __clk_get_rate(clk->parent));
  else if (clk->parent)
    clk->rate = clk->parent->rate;
  else
    clk->rate = 0;

  list_for_every_entry_safe(&g_clk_orphan_list, orphan, temp_orphan, struct clk, child_node)
    {
      if (orphan->num_parents && orphan->ops->get_parent)
        {
          i = orphan->ops->get_parent(orphan);
          if (!strcmp(clk->name, orphan->parent_names[i]))
            __clk_reparent(orphan, clk);
        }
      else if (orphan->num_parents)
        {
          for (i = 0; i < orphan->num_parents; i++)
            {
              if (!strcmp(clk->name, orphan->parent_names[i]))
                {
                  __clk_reparent(orphan, clk);
                  break;
                }
            }
        }
    }

  if (clk->ops->init)
    clk->ops->init(clk);
out:
  clk_unlock();

  return ret;
}


/****************************************************************************
   * Public Functions
****************************************************************************/

void clk_disable(struct clk *clk)
{
  clk_lock();
  __clk_disable(clk);
  clk_unlock();
}

int clk_enable(struct clk *clk)
{
  int ret;

  clk_lock();
  ret = __clk_enable(clk);
  clk_unlock();

  return ret;
}

int64_t clk_round_rate(struct clk *clk, uint64_t rate)
{
  uint64_t ret;

  clk_lock();
  ret = __clk_round_rate(clk, rate);
  clk_unlock();

  return ret;
}

int clk_set_rate(struct clk *clk, uint64_t rate)
{
  int ret = 0;
  struct clk *top;
  uint64_t parent_rate;

  if (!clk)
    return 0;

  clk_lock();

  if (rate == __clk_get_rate(clk))
    goto out;

  if ((clk->flags & CLK_SET_RATE_GATE) && clk->enable_count)
    {
      ret = -EBUSY;
      goto out;
    }

  top = clk_calc_new_rates(clk, rate);
  if (!top)
    {
      ret = -EINVAL;
      goto out;
    }

  if (top->new_parent)
    parent_rate = top->new_parent->rate;
  else if (top->parent)
    parent_rate = top->parent->rate;
  else
    parent_rate = 0;

  clk_change_rate(top, parent_rate);

out:
  clk_unlock();

  return ret;
}

int clk_set_phase(struct clk *clk, int degrees)
{
  int ret = -EINVAL;

  if (!clk)
    return 0;

  degrees %= 360;
  if (degrees < 0)
    degrees += 360;

  clk_lock();

  if (clk->ops->set_phase)
    ret = clk->ops->set_phase(clk, degrees);

  if (!ret)
    clk->phase = degrees;

  clk_unlock();

  return ret;
}

int clk_get_phase(struct clk *clk)
{
  int ret;

  if (!clk)
    return 0;

  clk_lock();
  ret = clk->phase;
  clk_unlock();

  return ret;
}

const char *clk_get_name(const struct clk *clk)
{
  return !clk ? NULL : clk->name;
}

uint32_t clk_get_enable_count(struct clk *clk)
{
  return !clk ? 0 : clk->enable_count;
}

struct clk *clk_get(const char *name)
{
  struct clk *root_clk = NULL;
  struct clk *ret = NULL;

  if (!name)
    return NULL;

  clk_lock();

  list_for_every_entry(&g_clk_root_list, root_clk, struct clk, child_node)
    {
      ret = __clk_lookup(name, root_clk);
      if (ret)
        goto out;
    }

  list_for_every_entry(&g_clk_orphan_list, root_clk, struct clk, child_node)
    {
      ret = __clk_lookup(name, root_clk);
      if (ret)
        goto out;
    }

  ret = NULL;

out:
  clk_unlock();

  return ret;
}

int clk_set_parent(struct clk *clk, struct clk *parent)
{
  struct clk *old_parent = NULL;
  int ret = 0;
  int index = 0;

  if (!clk)
    return 0;

  if ((clk->num_parents > 1) && (!clk->ops->set_parent))
    return -ENOSYS;

  clk_lock();

  if (clk->parent == parent)
    goto out;

  if ((clk->flags & CLK_SET_PARENT_GATE) && clk->enable_count)
    {
      ret = -EBUSY;
      goto out;
    }

  if (parent)
    {
      index = clk_fetch_parent_index(clk, parent);
      if (index < 0)
        {
          clkerr("clk %s can no be parent of clk %s\n",
              parent->name, core->name);
          ret = index;
          goto out;
        }
    }

  old_parent = clk->parent;

  if (clk->enable_count)
    {
      clk_enable(parent);
      clk_enable(clk);
    }

  clk_reparent(clk, parent);

  if (parent && clk->ops->set_parent)
    ret = clk->ops->set_parent(clk, index);

  if (ret)
    {
      clk_reparent(clk, old_parent);

      if (clk->enable_count)
      {
        clk_disable(clk);
        clk_disable(parent);
      }
      goto out;
    }

  if (clk->enable_count)
    {
      clk_disable(clk);
      clk_disable(old_parent);
    }

  __clk_recalc_rate(clk);

out:
  clk_unlock();

  return ret;
}

struct clk *clk_get_parent_by_index(struct clk *clk, uint8_t index)
{
  if (!clk || index >= clk->num_parents)
    return NULL;

  if (!clk->parents[index])
    clk->parents[index] = clk_get(clk->parent_names[index]);

  return clk->parents[index];
}

struct clk *clk_get_parent(struct clk *clk)
{
  struct clk *parent;

  clk_lock();
  parent = !clk ? NULL : clk->parent;
  clk_unlock();

  return parent;
}

uint64_t clk_get_rate(struct clk *clk)
{
  uint64_t rate;

  if (!clk)
      return 0;

  clk_lock();

  if (clk->flags & CLK_GET_RATE_NOCACHE)
    __clk_recalc_rate(clk);

  rate = __clk_get_rate(clk);

  clk_unlock();

  return rate;
}

struct clk *clk_register(const char *name, int32_t num_parents, const char **parent_names,
                        uint64_t flags, const struct clk_ops *ops, void *private_data)
{
  struct clk *clk = NULL;
  int i = 0;

  clk = kmm_zalloc(sizeof(struct clk));
  if (!clk)
    {
      return NULL;
    }

  clk->name = kmm_malloc(strlen(name) + 1);
  if (!clk->name)
    {
      goto fail_name;
    }

  strcpy((char *)clk->name, name);
  clk->ops = ops;
  clk->private_data = private_data;
  clk->num_parents = num_parents;
  clk->flags = flags;

  if (clk->num_parents)
    {
      clk->parent_names = kmm_calloc(clk->num_parents, sizeof(char *));

      if (!clk->parent_names)
        {
          goto fail_parent_names;
        }

      for (i = 0; i < clk->num_parents; i++)
        {
          clk->parent_names[i] = kmm_malloc(strlen(parent_names[i]) + 1);
          if (!clk->parent_names[i])
            {
              goto fail_parent_names_copy;
            }
          strcpy((char *)clk->parent_names[i], parent_names[i]);
        }

      clk->parents = kmm_calloc(clk->num_parents, sizeof(struct clk *));
      if (clk->parents)
        {
          goto fail_parent_names_copy;
        }
    }

  list_initialize(&clk->child_node);
  list_initialize(&clk->children);

  if (!__clk_register(clk))
    {
      return clk;
    }

  kmm_free(clk->parents);
fail_parent_names_copy:
  while (--i >= 0)
    kmm_free((void *)clk->parent_names[i]);
  kmm_free(clk->parent_names);
fail_parent_names:
  kmm_free((void *)clk->name);
fail_name:
  kmm_free(clk);
  return NULL;
}

#endif /* CONFIG_CLK */
