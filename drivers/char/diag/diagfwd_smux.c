/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/termios.h>
#include <linux/slab.h>
#include <linux/diagchar.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <mach/usbdiag.h>
#include "diagchar.h"
#include "diagfwd.h"
#include "diagfwd_smux.h"

void diag_smux_event(void *priv, int event_type, const void *metadata)
{
	unsigned char *rx_buf;
	int len;

	switch (event_type) {
	case SMUX_CONNECTED:
		pr_debug("diag: SMUX_CONNECTED received\n");
		driver->in_busy_smux = 0;
		
		queue_work(driver->diag_bridge_wq,
				 &(driver->diag_read_mdm_work));
		break;
	case SMUX_DISCONNECTED:
		pr_info("diag: SMUX_DISCONNECTED received\n");
		break;
	case SMUX_WRITE_DONE:
		pr_debug("diag: SMUX Write done\n");
		break;
	case SMUX_WRITE_FAIL:
		pr_info("diag: SMUX Write Failed\n");
		break;
	case SMUX_READ_FAIL:
		pr_info("diag: SMUX Read Failed\n");
		break;
	case SMUX_READ_DONE:
		len = ((struct smux_meta_read *)metadata)->len;
		rx_buf = ((struct smux_meta_read *)metadata)->buffer;
		driver->write_ptr_mdm->length = len;
		diag_device_write(driver->buf_in_smux, SMUX_DATA,
						 driver->write_ptr_mdm);
		break;
	};
}

int diagfwd_write_complete_smux(void)
{
	pr_debug("diag: clear in_busy_smux\n");
	driver->in_busy_smux = 0;
	return 0;
}

int diagfwd_read_complete_smux(void)
{
	queue_work(driver->diag_bridge_wq, &(driver->diag_read_mdm_work));
	return 0;
}

int diag_get_rx_buffer(void *priv, void **pkt_priv, void **buffer, int size)
{
	if (!driver->in_busy_smux) {
		*pkt_priv = (void *)0x1234;
		*buffer = driver->buf_in_smux;
		pr_debug("diag: set in_busy_smux as 1\n");
		driver->in_busy_smux = 1;
	} else {
		pr_debug("diag: read buffer for SMUX is BUSY\n");
		return -EAGAIN;
	}
	return 0;
}

static int diagfwd_smux_runtime_suspend(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: suspending...\n");
	return 0;
}

static int diagfwd_smux_runtime_resume(struct device *dev)
{
	dev_dbg(dev, "pm_runtime: resuming...\n");
	return 0;
}

static const struct dev_pm_ops diagfwd_smux_dev_pm_ops = {
	.runtime_suspend = diagfwd_smux_runtime_suspend,
	.runtime_resume = diagfwd_smux_runtime_resume,
};

int diagfwd_connect_smux(void)
{
	void *priv = NULL;
	int ret = 0;

	if (driver->lcid == LCID_INVALID) {
		ret = msm_smux_open(LCID_VALID, priv, diag_smux_event,
						 diag_get_rx_buffer);
		if (!ret) {
			driver->lcid = LCID_VALID;
			msm_smux_tiocm_set(driver->lcid, TIOCM_DTR, 0);
			pr_info("diag: open SMUX ch, r = %d\n", ret);
		} else {
			pr_err("diag: failed to open SMUX ch, r = %d\n", ret);
		}
	}
	
	queue_work(driver->diag_bridge_wq, &(driver->diag_read_mdm_work));
	return ret;
}

static int diagfwd_smux_probe(struct platform_device *pdev)
{
	int ret = 0;

	pr_info("diag: SMUX probe called\n");
	driver->lcid = LCID_INVALID;
	driver->diag_smux_enabled = 1;
	if (driver->buf_in_smux == NULL) {
		driver->buf_in_smux = kzalloc(IN_BUF_SIZE, GFP_KERNEL);
		if (driver->buf_in_smux == NULL)
			goto err;
	}
	ret = diagfwd_connect_smux();
	return ret;

err:
	pr_err("diag: Could not initialize SMUX buffer\n");
	kfree(driver->buf_in_smux);
	return ret;
}

struct platform_driver msm_diagfwd_smux_driver = {
	.probe = diagfwd_smux_probe,
	.driver = {
		   .name = "SMUX_DIAG",
		   .owner = THIS_MODULE,
		   .pm   = &diagfwd_smux_dev_pm_ops,
		   },
};
