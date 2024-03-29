/****************************************************************************
 * drivers/serial/uart_rpmsg.c
 * Serial driver for rpmsg UART
 *
 *   Copyright (C) 2017 Pinecone Inc. All rights reserved.
 *   Author: Guiding Li<liguiding@pinecone.net>
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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/fs/ioctl.h>
#include <nuttx/kmalloc.h>
#include <nuttx/rptun/openamp.h>
#include <nuttx/serial/serial.h>
#include <nuttx/serial/uart_rpmsg.h>

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define UART_RPMSG_DEV_CONSOLE          "/dev/console"
#define UART_RPMSG_DEV_PREFIX           "/dev/tty"
#define UART_RPMSG_EPT_PREFIX           "rpmsg-tty"

#define UART_RPMSG_TTY_WRITE            0
#define UART_RPMSG_TTY_WAKEUP           1

/****************************************************************************
 * Private Types
 ****************************************************************************/

begin_packed_struct struct uart_rpmsg_header_s
{
  uint32_t command : 31;
  uint32_t response : 1;
  int32_t  result;
  uint64_t cookie;
} end_packed_struct;

begin_packed_struct struct uart_rpmsg_write_s
{
  struct uart_rpmsg_header_s header;
  uint32_t                   count;
  uint32_t                   resolved;
  char                       data[0];
} end_packed_struct;

begin_packed_struct struct uart_rpmsg_wakeup_s
{
  struct uart_rpmsg_header_s header;
} end_packed_struct;

struct uart_rpmsg_priv_s
{
  struct rpmsg_endpoint ept;
  const char            *devname;
  const char            *cpuname;
  void                  *recv_data;
  bool                  last_upper;
#ifdef CONFIG_SERIAL_TERMIOS
  struct termios        termios;
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  uart_rpmsg_setup(FAR struct uart_dev_s *dev);
static void uart_rpmsg_shutdown(FAR struct uart_dev_s *dev);
static int  uart_rpmsg_attach(FAR struct uart_dev_s *dev);
static void uart_rpmsg_detach(FAR struct uart_dev_s *dev);
static int  uart_rpmsg_ioctl(FAR struct file *filep, int cmd, unsigned long arg);
static void uart_rpmsg_rxint(FAR struct uart_dev_s *dev, bool enable);
static bool uart_rpmsg_rxflowcontrol(FAR struct uart_dev_s *dev,
                    unsigned int nbuffered, bool upper);
static void uart_rpmsg_dmasend(FAR struct uart_dev_s *dev);
static void uart_rpmsg_dmareceive(FAR struct uart_dev_s *dev);
static void uart_rpmsg_dmarxfree(FAR struct uart_dev_s *dev);
static void uart_rpmsg_dmatxavail(FAR struct uart_dev_s *dev);
static void uart_rpmsg_send(FAR struct uart_dev_s *dev, int ch);
static void uart_rpmsg_txint(FAR struct uart_dev_s *dev, bool enable);
static bool uart_rpmsg_txready(FAR struct uart_dev_s *dev);
static bool uart_rpmsg_txempty(FAR struct uart_dev_s *dev);
static void uart_rpmsg_device_created(struct rpmsg_device *rdev, void *priv_);
static void uart_rpmsg_device_destroy(struct rpmsg_device *rdev, void *priv_);
static int  uart_rpmsg_ept_cb(struct rpmsg_endpoint *ept, void *data,
                              size_t len, uint32_t src, void *priv_);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_uart_rpmsg_ops =
{
  .setup         = uart_rpmsg_setup,
  .shutdown      = uart_rpmsg_shutdown,
  .attach        = uart_rpmsg_attach,
  .detach        = uart_rpmsg_detach,
  .ioctl         = uart_rpmsg_ioctl,
  .rxint         = uart_rpmsg_rxint,
  .rxflowcontrol = uart_rpmsg_rxflowcontrol,
  .dmasend       = uart_rpmsg_dmasend,
  .dmareceive    = uart_rpmsg_dmareceive,
  .dmarxfree     = uart_rpmsg_dmarxfree,
  .dmatxavail    = uart_rpmsg_dmatxavail,
  .send          = uart_rpmsg_send,
  .txint         = uart_rpmsg_txint,
  .txready       = uart_rpmsg_txready,
  .txempty       = uart_rpmsg_txempty,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int uart_rpmsg_setup(FAR struct uart_dev_s *dev)
{
  return OK;
}

static void uart_rpmsg_shutdown(FAR struct uart_dev_s *dev)
{
}

static int uart_rpmsg_attach(FAR struct uart_dev_s *dev)
{
  return OK;
}

static void uart_rpmsg_detach(FAR struct uart_dev_s *dev)
{
}

static int uart_rpmsg_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  int ret = -ENOTTY;

#ifdef CONFIG_SERIAL_TERMIOS
  struct uart_dev_s *dev = filep->f_inode->i_private;
  struct uart_rpmsg_priv_s *priv = dev->priv;

  switch (cmd)
    {
    case TCGETS:
      {
        struct termios *termiosp = (struct termios *)arg;

        if (termiosp)
          {
            *termiosp = priv->termios;
            ret = OK;
          }
        else
          {
            ret = -EINVAL;
          }
      }
      break;

    case TCSETS:
      {
        struct termios *termiosp = (struct termios *)arg;

        if (termiosp)
          {
            priv->termios = *termiosp;
            ret = OK;
          }
        else
          {
            ret = -EINVAL;
          }
      }
      break;
    }
#endif

  return ret;
}

static void uart_rpmsg_rxint(FAR struct uart_dev_s *dev, bool enable)
{
}

static bool uart_rpmsg_rxflowcontrol(FAR struct uart_dev_s *dev,
                unsigned int nbuffered, bool upper)
{
  struct uart_rpmsg_priv_s *priv = dev->priv;
  struct uart_rpmsg_wakeup_s msg;

  if (!upper && upper != priv->last_upper)
    {
      memset(&msg, 0, sizeof(msg));

      msg.header.command = UART_RPMSG_TTY_WAKEUP;
      if (is_rpmsg_ept_ready(&priv->ept))
        {
          rpmsg_send(&priv->ept, &msg, sizeof(msg));
        }
    }

  priv->last_upper = upper;
  return false;
}

static void uart_rpmsg_dmasend(FAR struct uart_dev_s *dev)
{
  struct uart_rpmsg_priv_s *priv = dev->priv;
  struct uart_dmaxfer_s *xfer = &dev->dmatx;
  struct uart_rpmsg_write_s *msg;
  size_t len = xfer->length + xfer->nlength;
  uint32_t space;

  msg = rpmsg_get_tx_payload_buffer(&priv->ept, &space, true);
  if (!msg)
    {
      dev->dmatx.length = 0;
      return;
    }

  memset(msg, 0, sizeof(*msg));

  space = C2B(space - sizeof(*msg));

  if (len > space)
    {
      len = space;
    }

  if (len > xfer->length)
    {
      cmem2bmem(msg->data, 0, xfer->buffer, xfer->length);
      cmem2bmem(msg->data + B2C_OFF(xfer->length), B2C_REM(xfer->length),
              xfer->nbuffer, len - xfer->length);
    }
  else
    {
      cmem2bmem(msg->data, 0, xfer->buffer, len);
    }

  msg->count          = len;
  msg->header.command = UART_RPMSG_TTY_WRITE;
  msg->header.result  = -ENXIO;
  msg->header.cookie  = (uintptr_t)dev;

  rpmsg_send_nocopy(&priv->ept, msg, sizeof(*msg) + B2C(len));
}

static void uart_rpmsg_dmareceive(FAR struct uart_dev_s *dev)
{
  struct uart_rpmsg_priv_s *priv = dev->priv;
  struct uart_dmaxfer_s *xfer = &dev->dmarx;
  struct uart_rpmsg_write_s *msg = priv->recv_data;
  uint32_t len = msg->count;
  size_t space = xfer->length + xfer->nlength;

  if (len > space)
    {
      len = space;
    }

  if (len > xfer->length)
    {
      bmem2cmem(xfer->buffer, msg->data, 0, xfer->length);
      bmem2cmem(xfer->nbuffer, msg->data + B2C_OFF(xfer->length),
              B2C_REM(xfer->length), len - xfer->length);
    }
  else
    {
      bmem2cmem(xfer->buffer, msg->data, 0, len);
    }

  xfer->nbytes = len;
  uart_recvchars_done(dev);

  msg->header.result = len;

  if (len != msg->count)
    {
      uart_rpmsg_rxflowcontrol(dev, 0, true);
    }
}

static void uart_rpmsg_dmarxfree(FAR struct uart_dev_s *dev)
{
}

static void uart_rpmsg_dmatxavail(FAR struct uart_dev_s *dev)
{
  struct uart_rpmsg_priv_s *priv = dev->priv;

  if (is_rpmsg_ept_ready(&priv->ept) && dev->dmatx.length == 0)
    {
      uart_xmitchars_dma(dev);
    }
}

static void uart_rpmsg_send(FAR struct uart_dev_s *dev, int ch)
{
  int nexthead;

  nexthead = dev->xmit.head + 1;
  if (nexthead >= dev->xmit.size)
    {
      nexthead = 0;
    }

  if (nexthead != dev->xmit.tail)
    {
      /* No.. not full.  Add the character to the TX buffer and return. */

      dev->xmit.buffer[dev->xmit.head] = ch;
      dev->xmit.head = nexthead;
    }

  uart_rpmsg_dmatxavail(dev);
}

static void uart_rpmsg_txint(FAR struct uart_dev_s *dev, bool enable)
{
}

static bool uart_rpmsg_txready(FAR struct uart_dev_s *dev)
{
  int nexthead;

  nexthead = dev->xmit.head + 1;
  if (nexthead >= dev->xmit.size)
    {
      nexthead = 0;
    }

  return nexthead != dev->xmit.tail;
}

static bool uart_rpmsg_txempty(FAR struct uart_dev_s *dev)
{
  return true;
}

static void uart_rpmsg_device_created(struct rpmsg_device *rdev, void *priv_)
{
  struct uart_dev_s *dev = priv_;
  struct uart_rpmsg_priv_s *priv = dev->priv;
  char eptname[RPMSG_NAME_SIZE];

  if (strcmp(priv->cpuname, rpmsg_get_cpuname(rdev)) == 0)
    {
      priv->ept.priv = dev;
      sprintf(eptname, "%s%s", UART_RPMSG_EPT_PREFIX, priv->devname);
      rpmsg_create_ept(&priv->ept, rdev, eptname,
                       RPMSG_ADDR_ANY, RPMSG_ADDR_ANY,
                       uart_rpmsg_ept_cb, NULL);
    }
}

static void uart_rpmsg_device_destroy(struct rpmsg_device *rdev, void *priv_)
{
  struct uart_dev_s *dev = priv_;
  struct uart_rpmsg_priv_s *priv = dev->priv;

  if (strcmp(priv->cpuname, rpmsg_get_cpuname(rdev)) == 0)
    {
      rpmsg_destroy_ept(&priv->ept);
    }
}

static int uart_rpmsg_ept_cb(struct rpmsg_endpoint *ept, void *data,
                             size_t len, uint32_t src, void *priv_)
{
  struct uart_dev_s *dev = priv_;
  struct uart_rpmsg_header_s *header = data;
  struct uart_rpmsg_write_s *msg = data;

  if (header->response)
    {
      /* Get write-cmd reponse, this tell how many data have sent */

      dev->dmatx.nbytes = header->result;
      if (header->result < 0)
        {
          dev->dmatx.nbytes = 0;
        }

      uart_xmitchars_done(dev);

      /* If have sent some data succeed, then continue send */

      if (msg->count == header->result)
        {
          uart_rpmsg_dmatxavail(dev);
        }
     }
  else if (header->command == UART_RPMSG_TTY_WRITE)
    {
      struct uart_rpmsg_priv_s *priv = dev->priv;

      /* Get write-cmd, there are some data, we need receive them */

      priv->recv_data = data;
      uart_recvchars_dma(dev);
      priv->recv_data = NULL;

      header->response = 1;
      rpmsg_send(ept, msg, sizeof(*msg));
    }
  else if (header->command == UART_RPMSG_TTY_WAKEUP)
    {
      /* Remote core have space, then wakeup current core, continue send */

      uart_rpmsg_dmatxavail(dev);
    }

  return 0;
}

/****************************************************************************
 * Public Funtions
 ****************************************************************************/

int uart_rpmsg_init(const char *cpuname, const char *devname,
                    int buf_size, bool isconsole)
{
  struct uart_rpmsg_priv_s *priv;
  struct uart_dev_s *dev;
  char dev_name[32];
  int ret = -ENOMEM;

  dev = kmm_zalloc(sizeof(struct uart_dev_s));
  if (!dev)
    {
      return ret;
    }

  dev->ops       = &g_uart_rpmsg_ops;
  dev->isconsole = isconsole;
  dev->recv.size = buf_size;
  dev->xmit.size = buf_size;

  dev->recv.buffer = kmm_malloc(dev->recv.size);
  if (!dev->recv.buffer)
    {
      goto fail;
    }

  dev->xmit.buffer = kmm_malloc(dev->xmit.size);
  if (!dev->xmit.buffer)
    {
      goto fail;
    }

  priv = kmm_zalloc(sizeof(struct uart_rpmsg_priv_s));
  if (!priv)
    {
      goto fail;
    }

  priv->cpuname = cpuname;
  priv->devname = devname;

  dev->priv = priv;

  ret = rpmsg_register_callback(dev,
                                uart_rpmsg_device_created,
                                uart_rpmsg_device_destroy,
                                NULL);
  if (ret < 0)
    {
      goto fail;
    }

  sprintf(dev_name, "%s%s", UART_RPMSG_DEV_PREFIX, devname);
  uart_register(dev_name, dev);

  if (dev->isconsole)
    {
      uart_register(UART_RPMSG_DEV_CONSOLE, dev);
    }

  return OK;

fail:
  kmm_free(dev->recv.buffer);
  kmm_free(dev->xmit.buffer);
  kmm_free(dev->priv);
  kmm_free(dev);

  return ret;
}

#ifdef CONFIG_RPMSG_SERIALINIT
/* dummy function to make linker happy */
void up_earlyserialinit(void)
{
}

void up_serialinit(void)
{
}
#endif /* CONFIG_RPMSG_SERIALINIT */
