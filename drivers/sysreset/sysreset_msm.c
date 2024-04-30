/* SPDX-License-Identifier: GPL-2.0+ */

#include <common.h>
#include <dm.h>
#include <sysreset.h>
#include <asm/io.h>

struct msm_sysreset_data {
    void __iomem *base;
};

int msm_sysreset_request(struct udevice *dev, enum sysreset_t type)
{
  struct msm_sysreset_data *data = dev_get_priv(dev);
  switch (type) {
    case SYSRESET_WARM:
    case SYSRESET_COLD:
    case SYSRESET_POWER:
    case SYSRESET_POWER_OFF:
      writel(0, data->base);
      return -EINPROGRESS;
    default:
      return -EPROTONOSUPPORT;
  }
}

static int msm_sysreset_probe(struct udevice *dev)
{
  struct msm_sysreset_data *data = dev_get_priv(dev);
  data->base = dev_remap_addr(dev);
  return 0;
}

static struct sysreset_ops msm_sysreset = {
        .request	= msm_sysreset_request,
};

static const struct udevice_id msm_sysreset_ids[] = {
        { .compatible = "qcom,pshold" },
        { }
};

U_BOOT_DRIVER(sysreset_msm) = {
        .id	= UCLASS_SYSRESET,
        .name	= "msm_sysreset",
        .priv_auto = sizeof(struct msm_sysreset_data),
        .ops = &msm_sysreset,
        .probe = msm_sysreset_probe,
        .of_match = msm_sysreset_ids,
};
