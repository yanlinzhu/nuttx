/****************************************************************************
 * arch/arm/src/song/u1_ap.c
 *
 *   Copyright (C) 2017 Pinecone Inc. All rights reserved.
 *   Author: Xiang Xiao <xiaoxiang@pinecone.net>
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

#include <nuttx/clk/clk-provider.h>
#include <nuttx/dma/song_dmas.h>
#include <nuttx/fs/hostfs_rpmsg.h>
#include <nuttx/ioexpander/song_ioe.h>
#include <nuttx/mbox/song_mbox.h>
#include <nuttx/power/regulator.h>
#include <nuttx/rptun/song_rptun.h>
#include <nuttx/serial/uart_16550.h>
#include <nuttx/serial/uart_rpmsg.h>
#include <nuttx/syslog/syslog_rpmsg.h>
#include <nuttx/timers/arch_alarm.h>
#include <nuttx/timers/arch_rtc.h>
#include <nuttx/timers/dw_wdt.h>
#include <nuttx/timers/song_oneshot.h>
#include <nuttx/timers/song_rtc.h>

#include "chip.h"
#include "nvic.h"
#include "song_addrenv.h"
#include "song_idle.h"
#include "systick.h"
#include "up_arch.h"
#include "up_internal.h"

#ifdef CONFIG_ARCH_CHIP_U1_AP

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CPU_NAME_CP                 "cp"
#define CPU_NAME_SP                 "sp"

#define TOP_MAILBOX_BASE            (0xb0030000)

#define TOP_PWR_BASE                (0xb0040000)
#define TOP_PWR_AP_M4_RSTCTL        (TOP_PWR_BASE + 0x0e0)
#define TOP_PWR_AP_M4_INTR2SLP_MK0  (TOP_PWR_BASE + 0x14c)
#define TOP_PWR_AP_UNIT_PD_CTL      (TOP_PWR_BASE + 0x204)
#define TOP_PWR_SLPCTL_AP_M4        (TOP_PWR_BASE + 0x360)
#define TOP_PWR_AP_M4_TCM_PD_CTL    (TOP_PWR_BASE + 0x3ec)

#define TOP_PWR_AP_M4_SFRST         (1 << 4)
#define TOP_PWR_AP_M4_IDLE_MK       (1 << 5)

#define TOP_PWR_AP_M4_PD_MK         (1 << 3)
#define TOP_PWR_AP_M4_AU_PU_MK      (1 << 6)
#define TOP_PWR_AP_M4_AU_PD_MK      (1 << 7)

#define TOP_PWR_AP_M4_SLP_EN        (1 << 0)
#define TOP_PWR_AP_M4_DS_SLP_EN     (1 << 2)

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_ARCH_DMA
static FAR struct dma_dev_s *g_dma[2] =
{
  [1] = DEV_END,
};
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern uint32_t _slog;
extern uint32_t _logsize;

#ifdef CONFIG_SONG_IOE
FAR struct ioexpander_dev_s *g_ioe[2] =
{
  [1] = DEV_END,
};
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_earlyinitialize(void)
{
  static const struct song_addrenv_s addrenv[] =
  {
    {.va = 0x21000000, .pa = 0xb1000000, .size = 0x00100000},
    {.va = 0x00000000, .pa = 0x00000000, .size = 0x00000000},
  };

  up_addrenv_initialize(addrenv);

  /* Unmask SLEEPING for reset */
  putreg32(TOP_PWR_AP_M4_IDLE_MK << 16, TOP_PWR_AP_M4_RSTCTL);

  /* Always allow enter FLASH_S */
  putreg32(TOP_PWR_AP_M4_DS_SLP_EN << 16 |
           TOP_PWR_AP_M4_DS_SLP_EN, TOP_PWR_SLPCTL_AP_M4);

  /* Allow AP TCM to LP */
  putreg32(TOP_PWR_AP_M4_AU_PD_MK << 16, TOP_PWR_AP_M4_TCM_PD_CTL);

  /* Forbid the AP power down, AP will power down following SP */
  putreg32(TOP_PWR_AP_M4_AU_PD_MK << 16 |
           TOP_PWR_AP_M4_AU_PD_MK, TOP_PWR_AP_UNIT_PD_CTL);
}

#ifdef CONFIG_RTC_SONG
int up_rtc_initialize(void)
{
  static const struct song_rtc_config_s config =
  {
    .minor = 0,
    .base  = 0xb2020000,
    .irq   = 16,
    .index = 1,
  };

  up_rtc_set_lowerhalf(song_rtc_initialize(&config));
  return 0;
}
#endif

void up_wic_initialize(void)
{
  putreg32(0xffffffff, TOP_PWR_AP_M4_INTR2SLP_MK0);
}

void up_wic_enable_irq(int irq)
{
  if (irq >= NVIC_IRQ_FIRST)
    {
      modifyreg32(TOP_PWR_AP_M4_INTR2SLP_MK0, 1 << (irq - NVIC_IRQ_FIRST), 0);
    }
}

void up_wic_disable_irq(int irq)
{
  if (irq >= NVIC_IRQ_FIRST)
    {
      modifyreg32(TOP_PWR_AP_M4_INTR2SLP_MK0, 0, 1 << (irq - NVIC_IRQ_FIRST));
    }
}

#ifdef CONFIG_ARCH_DMA
void up_dmainitialize(void)
{
#  ifdef CONFIG_SONG_DMAS
  g_dma[0] = song_dmas_initialize(1, 0xb0020000, 28, "sp/dmas_hclk");
#  endif
}
#endif

#ifdef CONFIG_16550_UART
FAR struct dma_chan_s *uart_dmachan(uart_addrwidth_t base, unsigned int ident)
{
#  ifdef CONFIG_ARCH_DMA
  return g_dma[0] ? DMA_GET_CHAN(g_dma[0], ident) : NULL;
#  else
  return NULL;
#  endif
}
#endif

void arm_timer_initialize(void)
{
#ifdef CONFIG_ONESHOT_SONG
  static const struct song_oneshot_config_s config =
  {
    .minor      = -1,
    .base       = TOP_PWR_BASE,
    .irq        = 18,
    .c1_freq    = 8192000,
    .ctl_off    = 0x170,
    .calib_off  = 0x194,
    .c1_off     = 0x174,
    .c2_off     = 0x178,
    .spec_off   = 0x1a8,
    .intren_off = 0x128,
    .intrst_off = 0x134,
    .intr_bit   = 1,
  };

  up_alarm_set_lowerhalf(song_oneshot_initialize(&config));
#endif

#ifdef CONFIG_CPULOAD_PERIOD
  sched_period_extclk(systick_initialize(false, 32768, -1));
#endif
}

#ifdef CONFIG_RPMSG_UART
void rpmsg_serialinit(void)
{
  uart_rpmsg_server_init("SP", 1024);
  uart_rpmsg_server_init("CP", 1024);
  uart_rpmsg_server_init("AT", 1024);
  uart_rpmsg_server_init("GPS", 1024);
}
#endif

#ifdef CONFIG_OPENAMP
static void up_openamp_initialize(void)
{
  struct mbox_dev_s *mbox_ap, *mbox_cp, *mbox_sp;

  static const struct song_mbox_config_s mbox_cfg_ap =
  {
    .base       = TOP_MAILBOX_BASE,
    .set_off    = 0x10,
    .en_off     = 0x14,
    .en_bit     = 16,
    .src_en_off = 0x14,
    .sta_off    = 0x18,
    .chnl_count = 16,
    .irq        = 21,
  };

  static const struct song_mbox_config_s mbox_cfg_cp =
  {
    .base       = TOP_MAILBOX_BASE,
    .set_off    = 0x0,
    .en_off     = 0x4,
    .en_bit     = 16,
    .src_en_off = 0x4,
    .sta_off    = 0x8,
    .chnl_count = 16,
    .irq        = -1,
  };

  static const struct song_mbox_config_s mbox_cfg_sp =
  {
    .base       = TOP_MAILBOX_BASE,
    .set_off    = 0x20,
    .en_off     = 0x24,
    .en_bit     = 16,
    .src_en_off = 0x24,
    .sta_off    = 0x28,
    .chnl_count = 16,
    .irq        = -1,
  };

  static const struct song_rptun_config_s rptun_cfg_cp =
  {
    .cpu_name    = CPU_NAME_CP,
    .role        = RPMSG_MASTER,
    .ch_start_tx = 2,
    .ch_vring_tx = 3,
    .ch_start_rx = 2,
    .ch_vring_rx = 3,
    .rsc         =
    {
      .rsc_tab   = (void *)0xb0003000,
      .size      = sizeof(struct rptun_rsc_s),
    },
  };

  static const struct song_rptun_config_s rptun_cfg_sp =
  {
    .cpu_name    = CPU_NAME_SP,
    .role        = RPMSG_REMOTE,
    .ch_start_tx = 14,
    .ch_vring_tx = 15,
    .ch_start_rx = 14,
    .ch_vring_rx = 15,
    .rsc         =
    {
      .rsc_tab   = (void *)0xb0000000,
      .size      = sizeof(struct rptun_rsc_s),
    },
  };

  mbox_ap = song_mbox_initialize(&mbox_cfg_ap);
  mbox_cp = song_mbox_initialize(&mbox_cfg_cp);
  mbox_sp = song_mbox_initialize(&mbox_cfg_sp);

  song_rptun_initialize(&rptun_cfg_cp, mbox_cp, mbox_ap);
  song_rptun_initialize(&rptun_cfg_sp, mbox_sp, mbox_ap);

#  ifdef CONFIG_SYSLOG_RPMSG_SERVER
  syslog_rpmsg_server_init();
#  endif

#  ifdef CONFIG_CLK_RPMSG
  clk_rpmsg_initialize(false);
#  endif

#  ifdef CONFIG_FS_HOSTFS_RPMSG
  hostfs_rpmsg_init(CPU_NAME_SP);
#  endif
}
#endif

#ifdef CONFIG_WATCHDOG_DW
void up_wdtinit(void)
{
  static const struct dw_wdt_config_s config =
  {
    .path = CONFIG_WATCHDOG_DEVPATH,
    .base = 0xb0090000,
    .irq  = 20,
    .tclk = "sp/apwdt_tclk",
  };

  dw_wdt_initialize(&config);
}
#endif

void up_lateinitialize(void)
{
#ifdef CONFIG_OPENAMP
  up_openamp_initialize();
#endif

#ifdef CONFIG_WATCHDOG_DW
  up_wdtinit();
#endif

#ifdef CONFIG_SONG_IOE
  g_ioe[0] = song_ioe_initialize(1, 0xb0060000, 19);
#endif

#ifdef CONFIG_RPMSG_REGULATOR
  rpmsg_regulator_init(CPU_NAME_SP, 0);
#endif
}

void up_reset(int status)
{
  putreg32(TOP_PWR_AP_M4_SFRST << 16 |
           TOP_PWR_AP_M4_SFRST, TOP_PWR_AP_M4_RSTCTL);
}

void up_cpu_doze(void)
{
  /* Forbid the full chip power down */
  putreg32(TOP_PWR_AP_M4_SLP_EN << 16, TOP_PWR_SLPCTL_AP_M4);

  /* Forbid the deep sleep */
  putreg32(getreg32(NVIC_SYSCON) & ~NVIC_SYSCON_SLEEPDEEP, NVIC_SYSCON);

  up_cpu_wfi();
}

void up_cpu_idle(void)
{
  /* Forbid the full chip power down */
  putreg32(TOP_PWR_AP_M4_SLP_EN << 16, TOP_PWR_SLPCTL_AP_M4);

  /* Allow the deep sleep */
  putreg32(getreg32(NVIC_SYSCON) | NVIC_SYSCON_SLEEPDEEP, NVIC_SYSCON);

  up_cpu_wfi();
}

void up_cpu_standby(void)
{
  up_cpu_idle();
}

void up_cpu_sleep(void)
{
  /* Allow the full chip power down */
  putreg32(TOP_PWR_AP_M4_SLP_EN << 16 |
           TOP_PWR_AP_M4_SLP_EN, TOP_PWR_SLPCTL_AP_M4);

  /* Allow the deep sleep */
  putreg32(getreg32(NVIC_SYSCON) | NVIC_SYSCON_SLEEPDEEP, NVIC_SYSCON);

  up_cpu_wfi();
}

#endif /* CONFIG_ARCH_CHIP_U1_AP */
