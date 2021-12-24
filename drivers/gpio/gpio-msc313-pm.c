// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Daniel Palmer
 */
//TODO test interrupts still work!
// SD interrupt is broken.

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/irqchip.h>
#include <dt-bindings/gpio/msc313-pm-gpio.h>

#define DRIVER_NAME	"gpio-msc313-pm"

#define BIT_OEN		BIT(0)
#define BIT_OUT		BIT(1)
#define BIT_IN		BIT(2)
#define BIT_IRQ_MASK	BIT(4)
#define BIT_IRQ_CLEAR	BIT(6)
#define BIT_IRQ_TYPE	BIT(7)

#define OFF_GPIO0	0x00
#define OFF_GPIO1	0x04
#define OFF_GPIO2	0x08
#define OFF_GPIO3	0x0c
#define OFF_GPIO4	0x10
#define OFF_GPIO5	0x14
#define OFF_GPIO6	0x18
#define OFF_GPIO7	0x1c
#define OFF_GPIO8	0x20
#define OFF_SPI_CZ	0x60
#define OFF_SPI_CK	0x64
#define OFF_SPI_DI	0x68
#define OFF_SPI_DO	0x6c
//#define OFF_IR_IN	0xa0
#define OFF_SPI_HLD	0x114
#define OFF_SD_CZ	0x11c

#define NAME_GPIO0	"pm_gpio0"
#define NAME_GPIO2	"pm_gpio2"
#define NAME_GPIO4	"pm_gpio4"
#define NAME_GPIO5	"pm_gpio5"
#define NAME_GPIO6	"pm_gpio6"
#define NAME_GPIO8	"pm_gpio8"
#define NAME_SPI_CZ	"pm_spi_cz"
#define NAME_SPI_CK	"pm_spi_ck"
#define NAME_SPI_DI	"pm_spi_di"
#define NAME_SPI_DO	"pm_spi_do"
#define NAME_SPI_HLD	"pm_spi_hld"
#define NAME_SD_SDZ	"pm_sd_sdz"

struct msc313_pm_gpio_data {
	const char **names;
	const unsigned *offsets;
	const unsigned num;
};

#define CHIP_DATA(chipname) \
	static const struct msc313_pm_gpio_data info_##chipname = \
	{\
		.names = chipname##_names,\
		.offsets = chipname##_offsets,\
		.num = ARRAY_SIZE(chipname##_offsets)\
	}

#ifdef CONFIG_MACH_INFINITY
static const char *msc313_names[] = {
	NAME_GPIO4,
	NAME_SD_SDZ
};

static const unsigned msc313_offsets[] = {
	OFF_GPIO4,
	OFF_SD_CZ
};

CHIP_DATA(msc313);
#endif /* infinity */

#ifdef CONFIG_MACH_MERCURY
static const char *ssc8336_names[] = {
	NAME_GPIO0,
	NAME_GPIO2,
	NAME_GPIO4,
	NAME_GPIO5,
	NAME_GPIO6,
	NAME_GPIO8,
	NAME_SPI_DO,
	NAME_SD_SDZ
};

static const unsigned ssc8336_offsets[] = {
	OFF_GPIO0,
	OFF_GPIO2,
	OFF_GPIO4,
	OFF_GPIO5,
	OFF_GPIO6,
	OFF_GPIO8,
	OFF_SPI_DO,
	OFF_SD_CZ
};

CHIP_DATA(ssc8336);
#endif /* mercury */

struct msc313_pm_gpio {
	struct device *dev;
	void __iomem *base;
	const struct msc313_pm_gpio_data *info;
};

static void msc313_pm_gpio_irq_eoi(struct irq_data *data)
{
	void __iomem *addr = data->chip_data;
	u16 reg = readw_relaxed(addr);

	reg |= BIT_IRQ_CLEAR;
	writew_relaxed(reg, addr);
	irq_chip_eoi_parent(data);
};

static void msc313_pm_gpio_irq_mask(struct irq_data *data)
{
	void __iomem *addr = data->chip_data;
	u16 reg = readw_relaxed(addr);

	reg |= BIT_IRQ_MASK;
	writew_relaxed(reg, addr);
	irq_chip_mask_parent(data);
};

static void msc313_pm_gpio_irq_unmask(struct irq_data *data)
{
	void __iomem *addr = data->chip_data;
	u16 reg = readw_relaxed(addr);
	reg &= ~BIT_IRQ_MASK;
	writew_relaxed(reg, addr);
	irq_chip_unmask_parent(data);
}

static int msc313_pm_gpio_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	void __iomem *addr = data->chip_data;
	u16 reg = readw_relaxed(addr);

	if(flow_type)
		reg &= BIT_IRQ_TYPE;
	else
		reg |= BIT_IRQ_TYPE;

	writew_relaxed(reg, addr);
	return 0;
}

static struct irq_chip msc313_pm_gpio_irqchip = {
	.name = "PM-GPIO",
	.irq_eoi = msc313_pm_gpio_irq_eoi,
	.irq_mask = msc313_pm_gpio_irq_mask,
	.irq_unmask = msc313_pm_gpio_irq_unmask,
	.irq_set_type = msc313_pm_gpio_irq_set_type,
};

static void msc313e_pm_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct msc313_pm_gpio *priv = gpiochip_get_data(chip);
	void __iomem *addr = priv->base + priv->info->offsets[offset];
	u16 reg = readw_relaxed(addr);

	if(value)
		reg |= BIT_OUT;
	else
		reg &= ~BIT_OUT;
	writew_relaxed(reg, addr);
}

static int msc313e_pm_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct msc313_pm_gpio *priv = gpiochip_get_data(chip);
	u16 reg = readw_relaxed(priv->base + priv->info->offsets[offset]);
	return reg & BIT_IN ? 1 : 0;
}

static int msc313e_pm_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct msc313_pm_gpio *priv = gpiochip_get_data(chip);
	void __iomem *addr = priv->base + priv->info->offsets[offset];
	u16 reg = readw_relaxed(addr);
	reg |= BIT_OEN;
	writew_relaxed(reg, addr);
	return 0;
}

static int msc313e_pm_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	struct msc313_pm_gpio *priv = gpiochip_get_data(chip);
	void __iomem *addr = priv->base + priv->info->offsets[offset];
	u16 reg = readw_relaxed(addr);
	reg &= ~BIT_OEN;
	writew_relaxed(reg, addr);
	return 0;
}

static int msc313e_pm_gpio_child_to_parent_hwirq(struct gpio_chip *chip,
					     unsigned int child,
					     unsigned int child_type,
					     unsigned int *parent,
					     unsigned int *parent_type)
{
	struct msc313_pm_gpio *priv = gpiochip_get_data(chip);

	/* first two interrupts on the parent aren't gpio,
	 * after that everything is the controller line number + 2.
	 */
	*parent_type = child_type;
	*parent = (priv->info->offsets[child] >> 2) + 2;

	return 0;
}

/*
 * The parent interrupt controller only takes one cell that is the number
 */
static void *msc313_pm_gpio_populate_parent_fwspec(struct gpio_chip *gc,
					     unsigned int parent_hwirq,
					     unsigned int parent_type)
{
	struct irq_fwspec *fwspec;

	fwspec = kmalloc(sizeof(*fwspec), GFP_KERNEL);
	if (!fwspec)
		return NULL;

	fwspec->fwnode = gc->irq.parent_domain->fwnode;
	fwspec->param_count = 1;
	fwspec->param[0] = parent_hwirq;

	return fwspec;
}

static int msc313_pm_gpio_probe(struct platform_device *pdev)
{
	int ret;
	struct msc313_pm_gpio *priv;
	struct gpio_chip *gpiochip;
	struct gpio_irq_chip *gpioirqchip;
	const struct msc313_pm_gpio_data *match_data;
	struct irq_domain *parent_domain;
	struct device_node *parent_node;

	match_data = of_device_get_match_data(&pdev->dev);
	if (!match_data)
		return -EINVAL;

	parent_node = of_irq_find_parent(pdev->dev.of_node);
	if (!parent_node)
		return -ENODEV;

	parent_domain = irq_find_host(parent_node);
	if (!parent_domain)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->info = match_data;

	platform_set_drvdata(pdev, priv);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	gpiochip = devm_kzalloc(&pdev->dev, sizeof(*gpiochip), GFP_KERNEL);
	if (!gpiochip)
		return -ENOMEM;

	gpiochip->label = DRIVER_NAME;
	gpiochip->parent = &pdev->dev;
	gpiochip->request = gpiochip_generic_request;
	gpiochip->free = gpiochip_generic_free;
	gpiochip->direction_input = msc313e_pm_gpio_direction_input;
	gpiochip->get = msc313e_pm_gpio_get;
	gpiochip->direction_output = msc313e_pm_gpio_direction_output;
	gpiochip->set = msc313e_pm_gpio_set;
	gpiochip->base = -1;
	gpiochip->ngpio = priv->info->num;
	gpiochip->names = priv->info->names;

	gpioirqchip = &gpiochip->irq;
	gpioirqchip->chip = &msc313_pm_gpio_irqchip;
	gpioirqchip->fwnode = of_node_to_fwnode(pdev->dev.of_node);
	gpioirqchip->parent_domain = parent_domain;
	gpioirqchip->child_to_parent_hwirq = msc313e_pm_gpio_child_to_parent_hwirq;
	gpioirqchip->populate_parent_alloc_arg = msc313_pm_gpio_populate_parent_fwspec;
	gpioirqchip->handler = handle_bad_irq;
	gpioirqchip->default_type = IRQ_TYPE_NONE;

	ret = devm_gpiochip_add_data(&pdev->dev, gpiochip, priv);
	if (ret < 0) {
		dev_err(priv->dev,"failed to register gpio chip");
		return ret;
	}

	return ret;
}

static const struct of_device_id msc313_pm_gpio_of_match[] = {
#ifdef CONFIG_MACH_INFINITY
	{
		/* MSC313, MSC313e */
		.compatible	= "mstar,msc313-gpio-pm",
		.data		= &info_msc313,
	},
	{
		/* SSD201, SSD202 */
		.compatible	= "mstar,ssd20xd-gpio-pm",
		.data		= &info_msc313,
	},
#endif
//#ifdef CONFIG_MACH_PIONEER
	{
		/* SSD212 */
		.compatible	= "sstar,ssd212-gpio-pm",
		.data		= &info_msc313,
	},
//#endif
#ifdef CONFIG_MACH_MERCURY
	{
		/* SSC8336, SSC8336N */
		.compatible	= "mstar,ssc8336-gpio-pm",
		.data		= &info_ssc8336,
	},
#endif
	{ }
};

static struct platform_driver msc313_pm_gpio_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = msc313_pm_gpio_of_match,
	},
	.probe = msc313_pm_gpio_probe,
};
builtin_platform_driver(msc313_pm_gpio_driver);
