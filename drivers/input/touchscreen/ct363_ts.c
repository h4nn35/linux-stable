/*
 *  Driver for VTL CT363 Touchscreen
 *
 *  Copyright (c) 2016 Johannes Pointner <johannes.pointner@gmail.com>
 *
 * TODO:Fix header
 *  This code is based on xx.c authored by xx@xx.xx:
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>

// Not used yet, not sure if this is even necessary
// static unsigned char binary_data[] = {
// #include "CT363_V01_0607_130603.dat"
// };

#define CT363_POINT_NUM		10
#define CT363_ABS_X_MAX		1024
#define CT363_ABS_Y_MAX		768

struct ct363_finger_data {
	unsigned char	xhi;			// X coordinate Hi
	unsigned char	yhi;			// Y coordinate Hi
	unsigned char	ylo : 4;		// Y coordinate Lo
	unsigned char	xlo : 4;		// X coordinate Lo
	unsigned char   status : 3;             // Action information, 1: Down; 2: Move; 3: Up
	unsigned char   id : 5;                 // ID information, from 1 to CFG_MAX_POINT_NUM
	unsigned char	area;			// Touch area
	unsigned char	pressure;		// Touch Pressure
};


struct ct363_priv{
	int press;
	int release;
	int x, y;
	union{
		struct ct363_finger_data pts[CT363_POINT_NUM];
		char buf[CT363_POINT_NUM * sizeof(struct ct363_finger_data)];
	};
};


struct ct363_ts {
	struct i2c_client	*client;
	struct input_dev	*input_dev;
	void 			*priv;
};

static irqreturn_t ct363_ts_interrupt(int irq, void *dev_id)
{
	struct ct363_ts *ts = dev_id;
	struct input_dev *input_dev = ts->input_dev;
	struct i2c_client *client = ts->client;
	int i, ret = 0;
	int sync = 0, x, y;
	int len = sizeof(struct ct363_finger_data) * CT363_POINT_NUM;
	struct ct363_priv *ct363 = ts->priv;

	ret = i2c_master_recv(client, ct363->buf, len);
	if(ret < 0){
		dev_warn(&client->dev, "Failed to read finger data\n");
		return;
	}

	ct363->press = 0;
	for(i = 0; i < CT363_POINT_NUM; i++){
		if((ct363->pts[i].xhi != 0xFF && ct363->pts[i].yhi != 0xFF) &&
			(ct363->pts[i].status == 1 || ct363->pts[i].status == 2)){
			x = (ct363->pts[i].xhi<<4)|(ct363->pts[i].xlo&0xF);
			y = (ct363->pts[i].yhi<<4)|(ct363->pts[i].ylo&0xF);

			// TODO:
			ct363->x = x;
			ct363->y = y;
			//ct363->x = ts->orientation[0] * x + ts->orientation[1] * y;
			//ct363->y = ts->orientation[2] * x + ts->orientation[3] * y;

			if( (ct363->x > CT363_ABS_X_MAX) || (ct363->y > CT363_ABS_Y_MAX) || (ct363->x < 0) || (ct363->y < 0) ){
				continue ;
			}

			input_mt_slot(input_dev, ct363->pts[i].id - 1);
			input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, true);
			input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(input_dev, ABS_MT_POSITION_X, ct363->x);
			input_report_abs(input_dev, ABS_MT_POSITION_Y, ct363->y);
			input_report_abs(input_dev, ABS_MT_PRESSURE, ct363->pts[i].pressure);

			sync = 1;
			ct363->press |= 0x01 << (ct363->pts[i].id - 1);
		}
	}
	ct363->release &= ct363->release ^ ct363->press;
	for(i = 0; i < CT363_POINT_NUM; i++){
		if ( ct363->release & (0x01<<i) ) {
			input_mt_slot(input_dev, i);
			input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, false);
			sync = 1;
		}
	}
	ct363->release = ct363->press;

	if(sync)
		input_sync(input_dev);

	return IRQ_HANDLED;
}

static int ct363_ts_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct ct363_ts *ts;
	struct input_dev *input_dev;
	struct ct363_priv *ct363;
	int error;

	ts = devm_kzalloc(&client->dev, sizeof(struct ct363_ts), GFP_KERNEL);
	if (!ts) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	ct363 = kzalloc(sizeof(struct ct363_priv), GFP_KERNEL);
	if(!ct363){
		dev_err(&client->dev, "No memory for ct36x");
		return -ENOMEM;
	}

	ts->priv = ct363;
	ts->client = client;
	ts->input_dev = input_dev;

	/* controller may be in sleep, wake it up. */
	/* TODO:
	error = egalax_wake_up_device(client);
	if (error) {
		dev_err(&client->dev, "Failed to wake up the controller\n");
		return error;
	}

	error = egalax_firmware_version(client);
	if (error < 0) {
		dev_err(&client->dev, "Failed to read firmware version\n");
		return error;
	}*/
	input_dev->name = "VTL ct363 Touch Screen";
	input_dev->id.bustype = BUS_I2C;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, CT363_ABS_X_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, CT363_ABS_Y_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_X, 0, CT363_ABS_X_MAX, 0, 0);
	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_Y, 0, CT363_ABS_Y_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_mt_init_slots(input_dev, 10, 0);

	input_set_drvdata(input_dev, ts);

	error = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					  ct363_ts_interrupt,
					  IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					  "ct363_ts", ts);
	if (error < 0) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		return error;
	}

	error = input_register_device(ts->input_dev);
	if (error)
		return error;

	i2c_set_clientdata(client, ts);
	return 0;
}

static const struct i2c_device_id ct363_ts_id[] = {
	{ "ct363_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ct363_ts_id);

static const struct of_device_id ct363_ts_dt_ids[] = {
	{ .compatible = "vtl,ct363_ts" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ct363_ts_dt_ids);

static struct i2c_driver ct363_ts_driver = {
	.probe		= ct363_ts_probe,
	.id_table	= ct363_ts_id,
	.driver = {
		.name	= "ct363_ts",
		.of_match_table	= ct363_ts_dt_ids,
	},

};

module_i2c_driver(ct363_ts_driver);

MODULE_AUTHOR("Johannes Pointner <johannes.pointner@gmail.com>");
MODULE_DESCRIPTION("CT363 Touchscreens Driver");
MODULE_LICENSE("GPL");

