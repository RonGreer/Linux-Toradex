// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for panels based on Sitronix ST7703 controller, souch as:
 *
 * - Rocktech jh057n00900 5.5" MIPI-DSI panel
 *
 * Copyright (C) Purism SPC 2019
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/media-bus-format.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/display_timing.h>
#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#define DRV_NAME "panel-sitronix-st7703"

/* Manufacturer specific Commands send via DSI */
#define ST7703_CMD_ALL_PIXEL_OFF 0x22
#define ST7703_CMD_ALL_PIXEL_ON	 0x23
#define ST7703_CMD_SETAPID	 0xB1
#define ST7703_CMD_SETDISP	 0xB2
#define ST7703_CMD_SETRGBIF	 0xB3
#define ST7703_CMD_SETCYC	 0xB4
#define ST7703_CMD_SETBGP	 0xB5
#define ST7703_CMD_SETVCOM	 0xB6
#define ST7703_CMD_SETOTP	 0xB7
#define ST7703_CMD_SETPOWER_EXT	 0xB8
#define ST7703_CMD_SETEXTC	 0xB9
#define ST7703_CMD_SETMIPI	 0xBA
#define ST7703_CMD_SETVDC	 0xBC
#define ST7703_CMD_UNKNOWN_BF	 0xBF
#define ST7703_CMD_SETSCR	 0xC0
#define ST7703_CMD_SETPOWER	 0xC1
#define ST7703_CMD_SETECO	 0xC6
#define ST7703_CMD_SETIO	 0xC7
#define ST7703_CMD_SETCABC	 0xC8
#define ST7703_CMD_SETPANEL	 0xCC
#define ST7703_CMD_SETGAMMA	 0xE0
#define ST7703_CMD_SETEQ	 0xE3
#define ST7703_CMD_SETGIP1	 0xE9
#define ST7703_CMD_SETGIP2	 0xEA
#define ST7703_CMD_UNKNOWN_EF	 0xEF

struct st7703 {
	struct device *dev;
	struct drm_panel panel;
	struct gpio_desc *reset_gpio;
	struct regulator *vcc;
	struct regulator *iovcc;
	bool prepared;

	struct dentry *debugfs;
	const struct st7703_panel_desc *desc;
};

struct st7703_panel_desc {
	const struct drm_display_mode *mode;
	unsigned int lanes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	int (*init_sequence)(struct st7703 *ctx);
};

static inline struct st7703 *panel_to_st7703(struct drm_panel *panel)
{
	return container_of(panel, struct st7703, panel);
}

static int jh057n_init_sequence(struct st7703 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	/*
	 * Init sequence was supplied by the panel vendor. Most of the commands
	 * resemble the ST7703 but the number of parameters often don't match
	 * so it's likely a clone.
	 */
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETEXTC,
				   0xF1, 0x12, 0x83);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETRGBIF,
				   0x10, 0x10, 0x05, 0x05, 0x03, 0xFF, 0x00, 0x00,
				   0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETSCR,
				   0x73, 0x73, 0x50, 0x50, 0x00, 0x00, 0x08, 0x70,
				   0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETVDC, 0x4E);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETPANEL, 0x0B);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETCYC, 0x80);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETDISP, 0xF0, 0x12, 0x30);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETEQ,
				   0x07, 0x07, 0x0B, 0x0B, 0x03, 0x0B, 0x00, 0x00,
				   0x00, 0x00, 0xFF, 0x00, 0xC0, 0x10);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETBGP, 0x08, 0x08);
	msleep(20);

	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETVCOM, 0x3F, 0x3F);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_UNKNOWN_BF, 0x02, 0x11, 0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETGIP1,
				   0x82, 0x10, 0x06, 0x05, 0x9E, 0x0A, 0xA5, 0x12,
				   0x31, 0x23, 0x37, 0x83, 0x04, 0xBC, 0x27, 0x38,
				   0x0C, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00,
				   0x03, 0x00, 0x00, 0x00, 0x75, 0x75, 0x31, 0x88,
				   0x88, 0x88, 0x88, 0x88, 0x88, 0x13, 0x88, 0x64,
				   0x64, 0x20, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
				   0x02, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETGIP2,
				   0x02, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x02, 0x46, 0x02, 0x88,
				   0x88, 0x88, 0x88, 0x88, 0x88, 0x64, 0x88, 0x13,
				   0x57, 0x13, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
				   0x75, 0x88, 0x23, 0x14, 0x00, 0x00, 0x02, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x0A,
				   0xA5, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_SETGAMMA,
				   0x00, 0x09, 0x0E, 0x29, 0x2D, 0x3C, 0x41, 0x37,
				   0x07, 0x0B, 0x0D, 0x10, 0x11, 0x0F, 0x10, 0x11,
				   0x18, 0x00, 0x09, 0x0E, 0x29, 0x2D, 0x3C, 0x41,
				   0x37, 0x07, 0x0B, 0x0D, 0x10, 0x11, 0x0F, 0x10,
				   0x11, 0x18);

	return 0;
}

static const struct drm_display_mode jh057n00900_mode = {
	.hdisplay    = 720,
	.hsync_start = 720 + 90,
	.hsync_end   = 720 + 90 + 20,
	.htotal	     = 720 + 90 + 20 + 20,
	.vdisplay    = 1440,
	.vsync_start = 1440 + 20,
	.vsync_end   = 1440 + 20 + 4,
	.vtotal	     = 1440 + 20 + 4 + 12,
	.clock	     = 75276,
	.flags	     = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm    = 65,
	.height_mm   = 130,
};

static const struct st7703_panel_desc jh057n00900_panel_desc = {
	.mode = &jh057n00900_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO |
		MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VIDEO_SYNC_PULSE,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = jh057n_init_sequence,
};

static int xbd599_init_sequence(struct st7703 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	/*
	 * Init sequence was supplied by the panel vendor.
	 */

	/* Magic sequence to unlock user commands below. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEXTC, 0xF1, 0x12, 0x83);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETMIPI,
			       0x33, /* VC_main = 0, Lane_Number = 3 (4 lanes) */
			       0x81, /* DSI_LDO_SEL = 1.7V, RTERM = 90 Ohm */
			       0x05, /* IHSRX = x6 (Low High Speed driving ability) */
			       0xF9, /* TX_CLK_SEL = fDSICLK/16 */
			       0x0E, /* HFP_OSC (min. HFP number in DSI mode) */
			       0x0E, /* HBP_OSC (min. HBP number in DSI mode) */
			       /* The rest is undocumented in ST7703 datasheet */
			       0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x44, 0x25, 0x00, 0x91, 0x0a, 0x00, 0x00, 0x02,
			       0x4F, 0x11, 0x00, 0x00, 0x37);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER_EXT,
			       0x25, /* PCCS = 2, ECP_DC_DIV = 1/4 HSYNC */
			       0x22, /* DT = 15ms XDK_ECP = x2 */
			       0x20, /* PFM_DC_DIV = /1 */
			       0x03  /* ECP_SYNC_EN = 1, VGX_SYNC_EN = 1 */);

	/* RGB I/F porch timing */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETRGBIF,
			       0x10, /* VBP_RGB_GEN */
			       0x10, /* VFP_RGB_GEN */
			       0x05, /* DE_BP_RGB_GEN */
			       0x05, /* DE_FP_RGB_GEN */
			       /* The rest is undocumented in ST7703 datasheet */
			       0x03, 0xFF,
			       0x00, 0x00,
			       0x00, 0x00);

	/* Source driving settings. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETSCR,
			       0x73, /* N_POPON */
			       0x73, /* N_NOPON */
			       0x50, /* I_POPON */
			       0x50, /* I_NOPON */
			       0x00, /* SCR[31,24] */
			       0xC0, /* SCR[23,16] */
			       0x08, /* SCR[15,8] */
			       0x70, /* SCR[7,0] */
			       0x00  /* Undocumented */);

	/* NVDDD_SEL = -1.8V, VDDD_SEL = out of range (possibly 1.9V?) */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVDC, 0x4E);

	/*
	 * SS_PANEL = 1 (reverse scan), GS_PANEL = 0 (normal scan)
	 * REV_PANEL = 1 (normally black panel), BGR_PANEL = 1 (BGR)
	 */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPANEL, 0x0B);

	/* Zig-Zag Type C column inversion. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETCYC, 0x80);

	/* Set display resolution. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETDISP,
			       0xF0, /* NL = 240 */
			       0x12, /* RES_V_LSB = 0, BLK_CON = VSSD,
				      * RESO_SEL = 720RGB
				      */
			       0xF0  /* WHITE_GND_EN = 1 (GND),
				      * WHITE_FRAME_SEL = 7 frames,
				      * ISC = 0 frames
				      */);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEQ,
			       0x00, /* PNOEQ */
			       0x00, /* NNOEQ */
			       0x0B, /* PEQGND */
			       0x0B, /* NEQGND */
			       0x10, /* PEQVCI */
			       0x10, /* NEQVCI */
			       0x00, /* PEQVCI1 */
			       0x00, /* NEQVCI1 */
			       0x00, /* reserved */
			       0x00, /* reserved */
			       0xFF, /* reserved */
			       0x00, /* reserved */
			       0xC0, /* ESD_DET_DATA_WHITE = 1, ESD_WHITE_EN = 1 */
			       0x10  /* SLPIN_OPTION = 1 (no need vsync after sleep-in)
				      * VEDIO_NO_CHECK_EN = 0
				      * ESD_WHITE_GND_EN = 0
				      * ESD_DET_TIME_SEL = 0 frames
				      */);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETECO, 0x01, 0x00, 0xFF, 0xFF, 0x00);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER,
			       0x74, /* VBTHS, VBTLS: VGH = 17V, VBL = -11V */
			       0x00, /* FBOFF_VGH = 0, FBOFF_VGL = 0 */
			       0x32, /* VRP  */
			       0x32, /* VRN */
			       0x77, /* reserved */
			       0xF1, /* APS = 1 (small),
				      * VGL_DET_EN = 1, VGH_DET_EN = 1,
				      * VGL_TURBO = 1, VGH_TURBO = 1
				      */
			       0xFF, /* VGH1_L_DIV, VGL1_L_DIV (1.5MHz) */
			       0xFF, /* VGH1_R_DIV, VGL1_R_DIV (1.5MHz) */
			       0xCC, /* VGH2_L_DIV, VGL2_L_DIV (2.6MHz) */
			       0xCC, /* VGH2_R_DIV, VGL2_R_DIV (2.6MHz) */
			       0x77, /* VGH3_L_DIV, VGL3_L_DIV (4.5MHz) */
			       0x77  /* VGH3_R_DIV, VGL3_R_DIV (4.5MHz) */);

	/* Reference voltage. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETBGP,
			       0x07, /* VREF_SEL = 4.2V */
			       0x07  /* NVREF_SEL = 4.2V */);
	msleep(20);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVCOM,
			       0x2C, /* VCOMDC_F = -0.67V */
			       0x2C  /* VCOMDC_B = -0.67V */);

	/* Undocumented command. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_UNKNOWN_BF, 0x02, 0x11, 0x00);

	/* This command is to set forward GIP timing. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP1,
			       0x82, 0x10, 0x06, 0x05, 0xA2, 0x0A, 0xA5, 0x12,
			       0x31, 0x23, 0x37, 0x83, 0x04, 0xBC, 0x27, 0x38,
			       0x0C, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00,
			       0x03, 0x00, 0x00, 0x00, 0x75, 0x75, 0x31, 0x88,
			       0x88, 0x88, 0x88, 0x88, 0x88, 0x13, 0x88, 0x64,
			       0x64, 0x20, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
			       0x02, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);

	/* This command is to set backward GIP timing. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP2,
			       0x02, 0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x02, 0x46, 0x02, 0x88,
			       0x88, 0x88, 0x88, 0x88, 0x88, 0x64, 0x88, 0x13,
			       0x57, 0x13, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
			       0x75, 0x88, 0x23, 0x14, 0x00, 0x00, 0x02, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x0A,
			       0xA5, 0x00, 0x00, 0x00, 0x00);

	/* Adjust the gamma characteristics of the panel. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGAMMA,
			       0x00, 0x09, 0x0D, 0x23, 0x27, 0x3C, 0x41, 0x35,
			       0x07, 0x0D, 0x0E, 0x12, 0x13, 0x10, 0x12, 0x12,
			       0x18, 0x00, 0x09, 0x0D, 0x23, 0x27, 0x3C, 0x41,
			       0x35, 0x07, 0x0D, 0x0E, 0x12, 0x13, 0x10, 0x12,
			       0x12, 0x18);

	return 0;
}

static const struct drm_display_mode xbd599_mode = {
	.hdisplay    = 720,
	.hsync_start = 720 + 40,
	.hsync_end   = 720 + 40 + 40,
	.htotal	     = 720 + 40 + 40 + 40,
	.vdisplay    = 1440,
	.vsync_start = 1440 + 18,
	.vsync_end   = 1440 + 18 + 10,
	.vtotal	     = 1440 + 18 + 10 + 17,
	.clock	     = 69000,
	.flags	     = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm    = 68,
	.height_mm   = 136,
};

static const struct st7703_panel_desc xbd599_desc = {
	.mode = &xbd599_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = xbd599_init_sequence,
};

static int fl7703ni_read_id(struct st7703 *ctx)
{
    u8 id1, id2, id3;
    int ret;
    struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

    ret = mipi_dsi_dcs_read(dsi, 0xDA, &id1, 1);
    if (ret < 0) {
        dev_err(ctx->dev, "LMI: could not read ID1 11\n");
        return ret;
    }

    ret = mipi_dsi_dcs_read(dsi, 0xDB, &id2, 1);
    if (ret < 0) {
        dev_err(ctx->dev, "LMI: could not read ID2 22\n");
        return ret;
    }

    ret = mipi_dsi_dcs_read(dsi, 0xDC, &id3, 1);
    if (ret < 0) {
        dev_err(ctx->dev, "LMI: could not read ID3\n");
        return ret;
    }

    dev_info(ctx->dev,
        "LMI: manufacturer: %02x version: %02x driver: %02x\n",
        id1, id2, id3);

    return 0;
}

static int nhd640480ef_init_sequence(struct st7703 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	msleep(50);
printk("[DEBUG] InST7703 init_sequence\n");
	fl7703ni_read_id(ctx);
	/*
	 * Init sequence was supplied by the panel vendor.
	 */
	dev_err(ctx->dev, "NHD640480EF_MSXP Init sequence\n");
	/* Magic sequence to unlock user commands below. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEXTC, 0xF1, 0x12, 0x87);
   /* Set display resolution. */

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETDISP,
			       0x78, /* NL = 120 - 1024Gate(480+136+4+0) */
			       0x14, //600RGB
			       0x70  );
	/* RGB I/F porch timing */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETRGBIF,
			       0x10, /* VBP_RGB_GEN */  
			       0x10, /* VFP_RGB_GEN */
			       0x28, /* DE_BP_RGB_GEN */
			       0x28, /* DE_FP_RGB_GEN */
			       /* The rest is undocumented in ST7703 datasheet */
			       0x03, 0xFF,
			       0x00, 0x00,
			       0x00, 0x00);

	/* Zig-Zag Type C column inversion. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETCYC, 0x80);
	/* Reference voltage. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETBGP,
			       0x0A, /* VREF_SEL  */
			       0x0A  /* NVREF_SEL */);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVCOM,
			       0x70, /* VCOMDC_F */
			       0x70  /* VCOMDC_B */);
				   
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER_EXT,
					0x26 );
			    //   0x25, /* PCCS = 2, ECP_DC_DIV = 1/4 HSYNC */
			    //   0x22, /* DT = 15ms XDK_ECP = x2 */
			    //   0x20, /* PFM_DC_DIV = /1 */
			    //   0x03  /* ECP_SYNC_EN = 1, VGX_SYNC_EN = 1 */);
	
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETMIPI,
			       0x31, /* VC_main = 0, Lane_Number = 1 (2 lanes) */
				   0x81,
				   0x05,
				   0xF9,
				   0x0E,
				   0x0E,
				   0x20,
				   0x00,
				   0x00,
				   0x00,
				   0x00,
				   0x00,
				   0x00,
				   0x00,
				   0x44,
				   0x25,
				   0x00,
				   0x91,
				   0x0a,
				   0x00,
				   0x00,
				   0x01,
				   0x4f,
				   0x01,
				   0x00,
				   0x00,
				   0x37);
//				   0x61,
//				   0x06,
//				   0xF9,
//				   0xFF,
//				   0x0A );
			       //0x81, /* DSI_LDO_SEL = 1.7V, RTERM = 90 Ohm */
			       //0x05, /* */
			       //0xF9, /* TX_CLK_SEL = fDSICLK/16 */
			       //0x0E, /* HFP_OSC (min. HFP number in DSI mode) */
			       //0x0E, /* HBP_OSC (min. HBP number in DSI mode) */
			       /* The rest is undocumented in ST7703 datasheet */
			       //0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       //0x44, 0x25, 0x00, 0x91, 0x0a, 0x00, 0x00, 0x01,
			       //0x4F, 0x01, 0x00, 0x00, 0x37);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVDC, 0x47);
	
	/* Undocumented command. Set PCR? */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_UNKNOWN_BF, 0x02, 0x11, 0x00);

	/* Source driving settings. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETSCR,
			       0x73, /* N_POPON */
			       0x73, /* N_NOPON */
			       0x50, /* I_POPON */
			       0x50, /* I_NOPON */
			       0x00, /* SCR[31,24] */
			       0x00, /* SCR[23,16] */
			       0x12, /* SCR[15,8] */
			       0x73, /* SCR[7,0] */
			       0x00  /* Undocumented */);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER,
			       0x54, /* */
			       0x00, /* FBOFF_VGH = 0, FBOFF_VGL = 0 */
			       0x32, /* VRP(?)  VSPR  */
			       0x32, /* VRN(?)  VSNR  */
			       0x77, /* reserved VSP VSN*/
			       0xF4, /* APS  F1         */
			       0x77, /*  */
			       0x77, /*  */
			       0xCC, /* VGH2_L_DIV, VGL2_L_DIV (2.6MHz) */
			       0xCC, /* VGH2_R_DIV, VGL2_R_DIV (2.6MHz) */
			       0xFF,
			       0xFF,
				   0x11,
				   0x11,
				   0x00,
				   0x00,
				   0x31 );
	
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETIO, 
				   0x10,
				   0x00,
				   0x0A,
				   0x00,
				   0x00,
				   0x00,
				   0x00,
				   0x00,
				   0xED,
				   0xC7,
				   0x00,
				   0xA5 );
	
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETCABC, 
				   0x10, 
				   0x40, 
				   0x1e,
			       0x03);

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPANEL, 
				  0x0B /*Forward: 0x0B, Backward:0x07*/); 
	
	/* Adjust the gamma characteristics of the panel. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGAMMA,
				   0x00, 0x05, 0x09, 0x29, 0x3C, 0x3F, 0x3B, 0x37,
				   0x05, 0x0A, 0x0C, 0x10, 0x13, 0x10, 0x13, 0x12,
				   0x1A, 0x00, 0x05, 0x09, 0x29, 0x3C, 0x3F, 0x3B,
				   0x37, 0x05, 0x0A, 0x0C, 0x10, 0x13, 0x10, 0x13,
				   0x12, 0x1A );		   
				   
	mipi_dsi_dcs_write_seq(dsi, 0xE1, 0x11, 0x11, 0x91, 0x00, 0x00, 0x00, 0x00 ); //0xE1 dylan, only FN7703?
	
				   
//        mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SOFT_RESET, 0x00);
//        msleep(80); 
				   

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEQ,
			       0x07, //1  PNOEQ 03                      
			       0x07, //2  NNOEQ 03                      
			       0x0B, //3  PEQGND                      
			       0x0B, //4  NEQGND                      
			       0x03, //5  PEQVCI  00                    
			       0x0B, //6  NEQVCI  07                    
			       0x00, //7  PEQVCI1                     
			       0x00, //8  NEQVCI1                     
			       0x00, //9  VCOM_PULLGND_OFF            
			       0x00, //10 VCOM_PULLGND_OFF            
			       0xFF, //11 VCOM_IDLE_ON             
			       0x04, //12 D4:Source blk pre charge/  D3:1X1 PATTERN DET  /D2:TCON USE DGC VS/  D1:D0:OPON DLY    84                  
			       0xC0, //13 defaut C0 ESD detect function   D5:D4:ESD OPT P/  D3:D2:SD LE/  D1:SDOPBIAS/  D0:TCON HS SYNC SEL
			       0x10 ); //14 SLPOTP
				   
	/* This command is to set forward GIP timing. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP1,
					0x01,  //1  PANSEL				   
					0x00,  //2  SHR_0[11:8]				   
					0x0E,  //3  SHR_0[7:0]  08				   
					0x00,  //4  SHR_1[11:8]				   
					0x00,  //5  SHR_1[7:0] 				   
					0xB0,  //6  SPON[7:0]                 				   
					0xB1,  //7  SPOFF[7:0]				   
					0x11,  //8  SHR0_1[3:0]);W_D( SHR0_2[3:0]				   
					0x31,  //9  SHR0_3[3:0]);W_D( SHR1_1[3:0]				   
					0x23,  //10 SHR1_2[3:0]);W_D( SHR1_3[3:0]				   
					0x28,  //11 SHP[3:0]);W_D( SCP[3:0]				   
					0x10,  //12 CHR[7:0]  0A				   
					0xB0,  //13 CON[7:0] 				   
					0xB1,  //14 COFF[7:0]				   
					0x27,  //15 CHP[3:0]);W_D( CCP[3:0]				   
					0x08,  //16 USER_GIP_GATE[7:0]           2A				   
					0x00,  //17 CGTS_L[21:16] 				   
					0x04,  //18 CGTS_L[15:8]    Gout11 STV				   
					0x02,  //19 CGTS_L[7:0]                Gout2  STV    				   
					0x00,  //20 CGTS_INV_L[21:16] 				   
					0x00,  //21 CGTS_INV_L[15:8]				   
					0x00,  //22 CGTS_INV_L[7:0]    GOUT5  INV  10				   
					0x00,  //23 CGTS_R[21:16]				   
					0x04,  //24 CGTS_R[15:8]   Gout11 STV				   
					0x02,  //25 CGTS_R[7:0]    Gout2  STV                                    				   
					0x00,  //26 CGTS_INV_R[21:16]				   
					0x00,  //27 CGTS_INV_R[15:8]				   
					0x00,  //28 CGTS_INV_R[7:0]  GOUT5  INV  10				   
					0x88,  //29 COS1_L[3:0] );W_D(COS2_L[3:0] );W_D(//  VSTF4U  VSTF2U				   
					0x88,  //30 COS3_L[3:0] );W_D(COS4_L[3:0] );W_D(//				   
					0xBA,  //31 COS5_L[3:0] );W_D(COS6_L[3:0] );W_D(//  FLCA  FLCB				   
					0x60,  //32 COS7_L[3:0] );W_D(COS8_L[3:0] );W_D(//  VCK1  VCK3				   
					0x24,  //33 COS9_L[3:0] );W_D(COS10_L[3:0]);W_D(//  VCK5  VCK7				   
					0x08,  //34 COS11_L[3:0]);W_D(COS12_L[3:0]);W_D(//  VSTF1D   VSTF3D				   
					0x88,  //35 COS13_L[3:0]);W_D(COS14_L[3:0]);W_D(//				   
					0x88,  //36 COS15_L[3:0]);W_D(COS16_L[3:0]);W_D(//         				   
					0x88,  //37 COS17_L[3:0]);W_D(COS18_L[3:0]);W_D(//  				   
					0x88,  //38 COS19_L[3:0]);W_D(COS20_L[3:0]);W_D(//				   
					0x88,  //39 COS21_L[3:0]);W_D(COS22_L[3:0]);W_D(//				   
					0x88,  //40 COS1_R[3:0] );W_D(COS2_R[3:0] );W_D(//   VSTF3U  VSTF1U				   
					0x88,  //41 COS3_R[3:0] );W_D(COS4_R[3:0] );W_D(//				   
					0xBA,  //42 COS5_R[3:0] );W_D(COS6_R[3:0] );W_D(//  FLCA  FLCB				   
					0x71,  //43 COS7_R[3:0] );W_D(COS8_R[3:0] );W_D(//  VCK2  VCK4				   
					0x35,  //44 COS9_R[3:0] );W_D(COS10_R[3:0]);W_D(//  VCK6  VCK8				   
					0x18,  //45 COS11_R[3:0]);W_D(COS12_R[3:0]);W_D(//  VSTT2D VSTF4D				   
					0x88,  //46 COS13_R[3:0]);W_D(COS14_R[3:0]);W_D(//				   
					0x88,  //47 COS15_R[3:0]);W_D(COS16_R[3:0]);W_D(//				   
					0x88,  //48 COS17_R[3:0]);W_D(COS18_R[3:0]);W_D(//				   
					0x88,  //49 COS19_R[3:0]);W_D(COS20_R[3:0]);W_D(//				   
					0x88,  //50 COS21_R[3:0]);W_D(COS22_R[3:0]);W_D(//				   
					0x00,  //51 TCONOPTION				   
					0x00,  //52 OPTION				   
					0x00,  //53 OTPION				   
					0x01,  //54 OPTION				   
					0x00,  //55 CHR2				   
					0x00,  //56 CON2				   
					0x00,  //57 COFF2				   
					0x00,  //58 CHP2);W_D(CCP2				   
					0x00,  //59 CKS 22 21 20 19 18 17				   
					0x00,  //60 CKS 16 15 14 13 12 11 10 9				   
					0x00,  //61 CKS 8 7 6 5 4 3 2 1                                          				   
					0x00,  //62 COFF[7:6]   CON[5:4]    SPOFF[3:2]    SPON[1:0]				   
					0x00);  //63 COFF2[7:6]  CON2[5:4]				   

	/* This command is to set backward GIP timing. */
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP2,
					0x97,  //1  ys2_sel[1:0]
					0x0A,  //2  user_gip_gate1[7:0]
					0x82,  //3  ck_all_on_width1[5:0]
					0x02,  //4  ck_all_on_width2[5:0]
					0x03,  //5  ck_all_on_width3[5:0]
					0x07,  //6  ys_flag_period[7:0]
					0x00,  //7  ys_2
					0x00,  //8  user_gip_gate1_2[7:0]
					0x00,  //9  ck_all_on_width1_2[5:0]
					0x00,  //10 ck_all_on_width2_2[5:0]
					0x00,  //11 ck_all_on_width3_2[5:0]
					0x00,  //12 ys_flag_period_2[7:0]
					0x81,  //13 COS1_L[3:0] );W_D(COS2_L[3:0] );W_D(//     VSTF4U  VSTF2U  
					0x88,  //14 COS3_L[3:0] );W_D(COS4_L[3:0] );W_D(//  
					0xBA,  //15 COS5_L[3:0] );W_D(COS6_L[3:0] );W_D(//     FLCA FLCB
					0x17,  //16 COS7_L[3:0] );W_D(COS8_L[3:0] );W_D(//     VCK1 VCK3    
					0x53,  //17 COS9_L[3:0] );W_D(COS10_L[3:0]);W_D(//     VCK5 VCL7
					0x88,  //18 COS11_L[3:0]);W_D(COS12_L[3:0]);W_D(// 
					0x88,  //19 COS13_L[3:0]);W_D(COS14_L[3:0]);W_D(//
					0x88,  //20 COS15_L[3:0]);W_D(COS16_L[3:0]);W_D(//         
					0x88,  //21 COS17_L[3:0]);W_D(COS18_L[3:0]);W_D(//  
					0x88,  //22 COS19_L[3:0]);W_D(COS20_L[3:0]);W_D(//
					0x88,  //23 COS21_L[3:0]);W_D(COS22_L[3:0]);W_D(//
					0x80,  //24 COS1_R[3:0] );W_D(COS2_R[3:0] );W_D(//     VSTF3U  VSTF1U
					0x88,  //25 COS3_R[3:0] );W_D(COS4_R[3:0] );W_D(//                     
					0xBA,  //26 COS5_R[3:0] );W_D(COS6_R[3:0] );W_D(//      FLCA FLCB 
					0x06,  //27 COS7_R[3:0] );W_D(COS8_R[3:0] );W_D(//      VCK2 VCK4
					0x42,  //28 COS9_R[3:0] );W_D(COS10_R[3:0]);W_D(//      VCK6 VCK8
					0x88,  //29 COS11_R[3:0]);W_D(COS12_R[3:0]);W_D(//
					0x88,  //30 COS13_R[3:0]);W_D(COS14_R[3:0]);W_D(//
					0x88,  //31 COS15_R[3:0]);W_D(COS16_R[3:0]);W_D(//
					0x88,  //32 COS17_R[3:0]);W_D(COS18_R[3:0]);W_D(//
					0x88,  //33 COS19_R[3:0]);W_D(COS20_R[3:0]);W_D(//
					0x88,  //34 COS21_R[3:0]);W_D(COS22_R[3:0]);W_D(
					0x23,  //35 EQOPT );W_D( EQ_SEL
					0x00,  //36 EQ_DELAY[7:0]                                    
					0x00,  //37 EQ_DELAY_HSYNC [3:0]
					0x02,  //38 HSYNC_TO_CL1_CNT9[8]
					0x80,  //39 HSYNC_TO_CL1_CNT9[7:0]        
					0x00,  //40 HIZ_L
					0x00,  //41 HIZ_R
					0x00,  //42 CKS_GS[21:16]
					0x00,  //43 CKS_GS[15:8]
					0x00,  //44 CKS_GS[7:0]
					0x00,  //45 CK_MSB_EN[21:16]
					0x00,  //46 CK_MSB_EN[15:8]
					0x00,  //47 CK_MSB_EN[7:0] 
					0x00,  //48 CK_MSB_EN_GS[21:16]
					0x00,  //49 CK_MSB_EN_GS[15:8]
					0x00,  //50 CK_MSB_EN_GS[7:0] 
					0x00,  //51  SHR2[11:8]
					0x00,  //52  SHR2[7:0]
					0x00,  //53  SHR2_1[3:0] SHR2_2
					0x00,  //54  SHR2_3[3:0]
					0x00,  //55 SHP1[3:0]);W_D(x
					0x00,  //56 SPON1[7:0]
					0x00,  //57 SPOFF1[7:0]
					0x00,  //58 SHP2[3:0]
					0x00,  //59 SPON2[7:0]
					0x00,  //60 SPOFF2[7:0]
					0x00);  //61 SPOFF2[9:8]/SPON2[9:8]/SPOFF1[9:8]/SPON1[9:8]

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_UNKNOWN_EF, 0xff, 0xff, 0x01);

	msleep(50);
	dev_err(ctx->dev, "NHD640480EF_init_sequence complete!\n");
	return 0;
}
static const struct drm_display_mode nhd640480ef_mode = {
	.hdisplay	= 640,
	.hsync_start	= 640 + 120,
	.hsync_end	= 640 + 120 + 120,
	.htotal		= 640 + 120 + 120 + 120,
	.vdisplay	= 480,
	.vsync_start	= 480 + 25,
	.vsync_end	= 480 + 25 + 5,
	.vtotal		= 480 + 25 + 5 + 12,
	.clock		= 31200,
	.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
//        .type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.width_mm	= 56,
	.height_mm	= 78,
};

static const struct st7703_panel_desc nhd640480ef_desc = {
	.mode = &nhd640480ef_mode,
	.lanes = 2,
	//.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = nhd640480ef_init_sequence, //fl7703_init_sequence,
};

static int rg353v2_init_sequence(struct st7703 *ctx)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	/*
	 * Init sequence was supplied by the panel vendor.
	 */

	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEXTC, 0xf1, 0x12, 0x83);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETAPID, 0x00, 0x00, 0x00,
			       0xda, 0x80);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETDISP, 0x00, 0x13, 0x70);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETRGBIF, 0x10, 0x10, 0x28,
			       0x28, 0x03, 0xff, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETCYC, 0x80);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETBGP, 0x0a, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVCOM, 0x92, 0x92);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER_EXT, 0x25, 0x22,
			       0xf0, 0x63);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETMIPI, 0x33, 0x81, 0x05,
			       0xf9, 0x0e, 0x0e, 0x20, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x44, 0x25, 0x00, 0x90, 0x0a,
			       0x00, 0x00, 0x01, 0x4f, 0x01, 0x00, 0x00, 0x37);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETVDC, 0x47);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_UNKNOWN_BF, 0x02, 0x11, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETSCR, 0x73, 0x73, 0x50, 0x50,
			       0x00, 0x00, 0x12, 0x50, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPOWER, 0x53, 0xc0, 0x32,
			       0x32, 0x77, 0xe1, 0xdd, 0xdd, 0x77, 0x77, 0x33,
			       0x33);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETECO, 0x82, 0x00, 0xbf, 0xff,
			       0x00, 0xff);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETIO, 0xb8, 0x00, 0x0a, 0x00,
			       0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETCABC, 0x10, 0x40, 0x1e,
			       0x02);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETPANEL, 0x0b);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGAMMA, 0x00, 0x07, 0x0d,
			       0x37, 0x35, 0x3f, 0x41, 0x44, 0x06, 0x0c, 0x0d,
			       0x0f, 0x11, 0x10, 0x12, 0x14, 0x1a, 0x00, 0x07,
			       0x0d, 0x37, 0x35, 0x3f, 0x41, 0x44, 0x06, 0x0c,
			       0x0d, 0x0f, 0x11, 0x10, 0x12, 0x14, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETEQ, 0x07, 0x07, 0x0b, 0x0b,
			       0x0b, 0x0b, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00,
			       0xc0, 0x10);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP1, 0xc8, 0x10, 0x02, 0x00,
			       0x00, 0xb0, 0xb1, 0x11, 0x31, 0x23, 0x28, 0x80,
			       0xb0, 0xb1, 0x27, 0x08, 0x00, 0x04, 0x02, 0x00,
			       0x00, 0x00, 0x00, 0x04, 0x02, 0x00, 0x00, 0x00,
			       0x88, 0x88, 0xba, 0x60, 0x24, 0x08, 0x88, 0x88,
			       0x88, 0x88, 0x88, 0x88, 0x88, 0xba, 0x71, 0x35,
			       0x18, 0x88, 0x88, 0x88, 0x88, 0x88, 0x00, 0x00,
			       0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_SETGIP2, 0x97, 0x0a, 0x82, 0x02,
			       0x03, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x81, 0x88, 0xba, 0x17, 0x53, 0x88, 0x88, 0x88,
			       0x88, 0x88, 0x88, 0x80, 0x88, 0xba, 0x06, 0x42,
			       0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x23, 0x00,
			       0x00, 0x02, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00);
	mipi_dsi_dcs_write_seq(dsi, ST7703_CMD_UNKNOWN_EF, 0xff, 0xff, 0x01);

	return 0;
}

static const struct drm_display_mode rg353v2_mode = {
	.hdisplay	= 640,
	.hsync_start	= 640 + 40,
	.hsync_end	= 640 + 40 + 2,
	.htotal		= 640 + 40 + 2 + 80,
	.vdisplay	= 480,
	.vsync_start	= 480 + 18,
	.vsync_end	= 480 + 18 + 2,
	.vtotal		= 480 + 18 + 2 + 28,
	.clock		= 24150,
	.flags		= DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm	= 70,
	.height_mm	= 57,
};

static const struct st7703_panel_desc rg353v2_desc = {
	.mode = &rg353v2_mode,
	.lanes = 4,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
		      MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_MODE_LPM,
	.format = MIPI_DSI_FMT_RGB888,
	.init_sequence = rg353v2_init_sequence,
};

static int st7703_enable(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = ctx->desc->init_sequence(ctx);
	if (ret < 0) {
		dev_err(ctx->dev, "Panel init sequence failed: %d\n", ret);
		return ret;
	}

	msleep(20);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}

	/* Panel is operational ?? msec after reset */
	msleep(250);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret)
		return ret;

	dev_dbg(ctx->dev, "Panel init sequence done\n");

	return 0;
}

static int st7703_disable(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	int ret;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(ctx->dev, "Failed to turn off the display: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		dev_err(ctx->dev, "Failed to enter sleep mode: %d\n", ret);

	return 0;
}

static int st7703_unprepare(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);

	if (!ctx->prepared)
		return 0;

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->iovcc);
	regulator_disable(ctx->vcc);
	ctx->prepared = false;

	return 0;
}

static int st7703_prepare(struct drm_panel *panel)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	dev_dbg(ctx->dev, "Resetting the panel\n");
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	ret = regulator_enable(ctx->iovcc);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable iovcc supply: %d\n", ret);
		return ret;
	}

	ret = regulator_enable(ctx->vcc);
	if (ret < 0) {
		dev_err(ctx->dev, "Failed to enable vcc supply: %d\n", ret);
		regulator_disable(ctx->iovcc);
		return ret;
	}

	/* Give power supplies time to stabilize before deasserting reset. */
	usleep_range(10000, 20000);

	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(15000, 20000);

	ctx->prepared = true;

	return 0;
}

static const u32 mantix_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
};

static int st7703_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct st7703 *ctx = panel_to_st7703(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, ctx->desc->mode);
	if (!mode) {
		dev_err(ctx->dev, "Failed to add mode %ux%u@%u\n",
			ctx->desc->mode->hdisplay, ctx->desc->mode->vdisplay,
			drm_mode_vrefresh(ctx->desc->mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	drm_display_info_set_bus_formats(&connector->display_info,
					 mantix_bus_formats,
					 ARRAY_SIZE(mantix_bus_formats));

	return 1;
}

static const struct drm_panel_funcs st7703_drm_funcs = {
	.disable   = st7703_disable,
	.unprepare = st7703_unprepare,
	.prepare   = st7703_prepare,
	.enable	   = st7703_enable,
	.get_modes = st7703_get_modes,
};

static int allpixelson_set(void *data, u64 val)
{
	struct st7703 *ctx = data;
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);

	dev_dbg(ctx->dev, "Setting all pixels on\n");
	mipi_dsi_generic_write_seq(dsi, ST7703_CMD_ALL_PIXEL_ON);
	msleep(val * 1000);
	/* Reset the panel to get video back */
	drm_panel_disable(&ctx->panel);
	drm_panel_unprepare(&ctx->panel);
	drm_panel_prepare(&ctx->panel);
	drm_panel_enable(&ctx->panel);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(allpixelson_fops, NULL,
			allpixelson_set, "%llu\n");

static void st7703_debugfs_init(struct st7703 *ctx)
{
	ctx->debugfs = debugfs_create_dir(DRV_NAME, NULL);

	debugfs_create_file("allpixelson", 0600, ctx->debugfs, ctx,
			    &allpixelson_fops);
}

static void st7703_debugfs_remove(struct st7703 *ctx)
{
	debugfs_remove_recursive(ctx->debugfs);
	ctx->debugfs = NULL;
}

static int st7703_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct st7703 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio), "Failed to get reset gpio\n");

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	ctx->desc = of_device_get_match_data(dev);

	dsi->mode_flags = ctx->desc->mode_flags;
	dsi->format = ctx->desc->format;
	dsi->lanes = ctx->desc->lanes;

	ctx->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(ctx->vcc))
		return dev_err_probe(dev, PTR_ERR(ctx->vcc), "Failed to request vcc regulator\n");

	ctx->iovcc = devm_regulator_get(dev, "iovcc");
	if (IS_ERR(ctx->iovcc))
		return dev_err_probe(dev, PTR_ERR(ctx->iovcc),
				     "Failed to request iovcc regulator\n");

	drm_panel_init(&ctx->panel, dev, &st7703_drm_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return ret;

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "mipi_dsi_attach failed (%d). Is host ready?\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(dev, "%ux%u@%u %ubpp dsi %udl - ready\n",
		 ctx->desc->mode->hdisplay, ctx->desc->mode->vdisplay,
		 drm_mode_vrefresh(ctx->desc->mode),
		 mipi_dsi_pixel_format_to_bpp(dsi->format), dsi->lanes);

	st7703_debugfs_init(ctx);
	return 0;
}

static void st7703_shutdown(struct mipi_dsi_device *dsi)
{
	struct st7703 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = drm_panel_unprepare(&ctx->panel);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to unprepare panel: %d\n", ret);

	ret = drm_panel_disable(&ctx->panel);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to disable panel: %d\n", ret);
}

static void st7703_remove(struct mipi_dsi_device *dsi)
{
	struct st7703 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	st7703_shutdown(dsi);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);

	st7703_debugfs_remove(ctx);
}

static const struct of_device_id st7703_of_match[] = {
	{ .compatible = "anbernic,rg353v-panel-v2", .data = &rg353v2_desc },
	{ .compatible = "rocktech,jh057n00900", .data = &jh057n00900_panel_desc },
	{ .compatible = "xingbangda,xbd599", .data = &xbd599_desc },
	{ .compatible = "forcelead,fl7703", .data = &nhd640480ef_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, st7703_of_match);

static struct mipi_dsi_driver st7703_driver = {
	.probe	= st7703_probe,
	.remove = st7703_remove,
	.shutdown = st7703_shutdown,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = st7703_of_match,
	},
};
module_mipi_dsi_driver(st7703_driver);

MODULE_AUTHOR("Guido Günther <agx@sigxcpu.org>");
MODULE_DESCRIPTION("DRM driver for Sitronix ST7703 based MIPI DSI panels");
MODULE_LICENSE("GPL v2");
