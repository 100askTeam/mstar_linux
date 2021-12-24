// SPDX-License-Identifier: GPL-2.0+

#include <dt-bindings/phy/phy.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/usb/mstar_usbc.h>
#include <linux/usb/mstar_utmi.h>
#include <linux/regulator/consumer.h>

#define NUM_PORTS 2
#define PORT_UHC 0
#define PORT_OTG 1

struct msc313_usb_phy {
	struct device *dev;
	struct phy *ports[NUM_PORTS];
	struct regmap *utmi;
	struct regmap *usbc;
	struct regulator *vbus;
};

static int msc313_usb_phy_init(struct phy *phy)
{
	struct msc313_usb_phy *msc313_usb_phy;

	msc313_usb_phy = phy_get_drvdata(phy);
	if (!msc313_usb_phy)
		return -ENODEV;

	return 0;
}

static const struct phy_ops msc313_usb_phy_ops = {
	.init = msc313_usb_phy_init,
	.owner = THIS_MODULE,
};

static struct phy *msc313_usb_phy_xlate(struct device *dev,
					struct of_phandle_args *args)
{
	struct msc313_usb_phy *msc313_usb_phy = dev_get_drvdata(dev);
	int port = args->args[0];

	printk("getting port %d\n", port);

	if (!msc313_usb_phy)
		return  ERR_PTR(-ENODEV);
	if(port >= NUM_PORTS)
		return ERR_PTR(-ENODEV);

	return msc313_usb_phy->ports[port];
}

static irqreturn_t msc313_usb_phy_irq(int irq, void *data)
{
        struct msc313_usb_phy *phy = data;
	printk("int\n");
	regmap_write(phy->usbc, MSTAR_USBC_REG_INTSTS, MSTAR_USBC_INT_MASK);
	return IRQ_HANDLED;
}

static void msc313_usb_phy_switch_port(struct msc313_usb_phy *phy)
{
	dev_info(phy->dev, "Switching to UHC port\n");
	regmap_update_bits(phy->usbc, MSTAR_USBC_REG_PRTCTRL,
				MSTAR_PRTCTRL_OTG | MSTAR_PRTCTRL_UHC, 0);
	regmap_update_bits(phy->usbc, MSTAR_USBC_REG_PRTCTRL,
				MSTAR_PRTCTRL_UHC, MSTAR_PRTCTRL_UHC);
	// not sure what this is, might be MAC power down
	//regmap_update_bits(msc313_usb_phy->usbc, MSTAR_USBC_REG_RSTCTRL,
	//			BIT(MSTAR_RSTCTRL_VBUSVALID),
	//			0);

}

static void msc313_usb_phy_do_calibration(struct msc313_usb_phy *phy){
	unsigned int calval;
	dev_info(phy->dev, "starting calibration...");
	regmap_update_bits(phy->utmi, MSTAR_UTMI_REG_CAL,
			MSTAR_UTMI_REG_CAL_START, MSTAR_UTMI_REG_CAL_START);
	mdelay(1);
	regmap_update_bits(phy->utmi, MSTAR_UTMI_REG_CAL,
			MSTAR_UTMI_REG_CAL_START, 0);
	if(regmap_read_poll_timeout(phy->utmi, MSTAR_UTMI_REG_CAL,
			calval, calval & MSTAR_UTMI_REG_CAL_END, 0, 1000000)){
		dev_info(phy->dev, "calibration timeout");
	}
	else {
		calval >>= MSTAR_UTMI_REG_CAL_DATA_SHIFT;
		if(calval > 0 && calval < 0xfff)
			dev_info(phy->dev, "calibration finished.");
		else
			dev_warn(phy->dev, "calibration failed.");
	}
}

static int msc313_usb_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct msc313_usb_phy *msc313_usb_phy;
	int i, irq;
	bool swap;

	msc313_usb_phy = devm_kzalloc(dev, sizeof(*msc313_usb_phy), GFP_KERNEL);
	if (!msc313_usb_phy)
		return  -ENOMEM;

	msc313_usb_phy->dev = dev;

	msc313_usb_phy->utmi = syscon_regmap_lookup_by_phandle(dev->of_node, "mstar,utmi");
	if(IS_ERR(msc313_usb_phy->utmi)){
		return PTR_ERR(msc313_usb_phy->utmi);
	}

	msc313_usb_phy->usbc = syscon_regmap_lookup_by_phandle(dev->of_node, "mstar,usbc");
	if (IS_ERR(msc313_usb_phy->usbc))
		return PTR_ERR(msc313_usb_phy->usbc);

	irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!irq){
		dev_warn(&pdev->dev, "no interrupt provided");
	}

	// hack for m5, these seem to be the reset values for i3
	regmap_write(msc313_usb_phy->usbc, MSTAR_USBC_REG_RSTCTRL,
			0x228);
	regmap_write(msc313_usb_phy->utmi, MSTAR_UTMI_REG_PLL_TEST1,
				0x2088);
	regmap_write(msc313_usb_phy->utmi, MSTAR_UTMI_REG_PLL_TEST0,
				0x8051);
	regmap_write(msc313_usb_phy->utmi, MSTAR_UTMI_REG_CONFIG,
			0x2084);

	//FIXME for some reason this doesn't update the registers.
	// some voodoo that is enabled for the msc313 in the vendor sdk
	regmap_write(msc313_usb_phy->usbc, MSTAR_USBC_REG_MIUCFG0,
					0x0);
	regmap_write(msc313_usb_phy->usbc, MSTAR_USBC_REG_MIUCFG1,
					0xffff);
	regmap_write(msc313_usb_phy->usbc, MSTAR_USBC_REG_MIUCFG2,
					BIT(8) | 0xff);

	// clear any existing interrupts and then enable
	// the interrupt
	regmap_write(msc313_usb_phy->usbc, MSTAR_USBC_REG_INTEN,
			MSTAR_USBC_INT_MASK);
	regmap_write(msc313_usb_phy->usbc, MSTAR_USBC_REG_INTSTS,
                        MSTAR_USBC_INT_MASK);
	if(irq){
		devm_request_irq(&pdev->dev, irq, msc313_usb_phy_irq, IRQF_SHARED,
				dev_name(&pdev->dev), msc313_usb_phy);
	}

	// power up hacks
	regmap_write(msc313_usb_phy->utmi, REG_CLKCTRL, 0x0c2f);
	regmap_write(msc313_usb_phy->utmi, REG_CLKCTRL, 0x040f);
	regmap_write(msc313_usb_phy->utmi, REG_PWRCTRL, 0x7f05);

	msc313_usb_phy_switch_port(msc313_usb_phy);

	regmap_write(msc313_usb_phy->utmi, REG_CLKCTRL, 0x0426);
	regmap_write(msc313_usb_phy->utmi, REG_PWRCTRL, 0x6bc3);
	regmap_write(msc313_usb_phy->utmi, REG_PWRCTRL, 0x69c3);
	regmap_write(msc313_usb_phy->utmi, REG_PWRCTRL, 0x0001);

	regmap_write(msc313_usb_phy->utmi, REG_EYESETTING1, 0x0210);
	regmap_write(msc313_usb_phy->utmi, REG_EYESETTING2, 0x8100);


	msc313_usb_phy_do_calibration(msc313_usb_phy);

	swap = of_property_read_bool(dev->of_node, "mstar,utmi-dxswap");

	if(swap)
		dev_info(dev, "enabling data line swap");
	regmap_update_bits(msc313_usb_phy->utmi, MSTAR_UTMI_REG_CLKINV,
			MSTAR_UTMI_REG_CLKINV_DPDNSWP, swap ? MSTAR_UTMI_REG_CLKINV_DPDNSWP : 0);

	//regmap_update_bits(msc313_usb_phy->regmap, REG_PWRCTRL, PWRCTRL_UPLL_PDN, 0);

	for(i = 0; i < NUM_PORTS; i++){
		msc313_usb_phy->ports[i] = devm_phy_create(dev, NULL, &msc313_usb_phy_ops);
		if (IS_ERR(msc313_usb_phy->ports[i])) {
			return PTR_ERR(msc313_usb_phy->ports[i]);
		}
		phy_set_drvdata(msc313_usb_phy->ports[i], msc313_usb_phy);
	}

	msc313_usb_phy->vbus = devm_regulator_get(dev, "vbus");
	regulator_enable(msc313_usb_phy->vbus);

	dev_set_drvdata(dev, msc313_usb_phy);

	phy_provider = devm_of_phy_provider_register(&pdev->dev,
						     msc313_usb_phy_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id msc313_usb_phy_ids[] = {
	{ .compatible = "mstar,msc313-usb-phy", },
	{ /* end of list */ },
};

static struct platform_driver msc313_usb_phy_driver = {
	.probe	= msc313_usb_phy_probe,
	.driver = {
		.of_match_table	= msc313_usb_phy_ids,
		.name  = "msc313-usb-phy",
	}
};
builtin_platform_driver(msc313_usb_phy_driver);
