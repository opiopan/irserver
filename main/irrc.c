#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "irrc.h"

#define CMDBUFFLEN 1024

#define RMT_RX_CHANNEL RMT_CHANNEL_0
#define RMT_TX_CHANNEL RMT_CHANNEL_4

#define RMT_CLK_DIV 80 /* APB clock is 80Mhz and RMT tick set to 1Mhz */

static struct {
    int32_t carrier;
    int32_t duty;
    int32_t unit;
} ProtocolDef[] = {
    {38000, 33, 562}, /* NEC */
    {38000, 33, 425}, /* AEHA */
    {40000, 33, 600}  /* SONY */
};
    
static void initTx(IRRC* ctx, IRRC_PROTOCOL protocol, int32_t gpio)
{
    ctx->rmt = (rmt_config_t){
	.rmt_mode = RMT_MODE_TX,
	.channel = RMT_TX_CHANNEL,
	.gpio_num = gpio,
	.mem_block_num = 4,
	.clk_div = RMT_CLK_DIV,
	.tx_config.loop_en = false,
	.tx_config.carrier_duty_percent = ProtocolDef[protocol].duty,
	.tx_config.carrier_freq_hz = ProtocolDef[protocol].carrier,
	.tx_config.carrier_level = RMT_CARRIER_LEVEL_HIGH,
	.tx_config.carrier_en = 1,
	.tx_config.idle_level = RMT_IDLE_LEVEL_LOW,
	.tx_config.idle_output_en = true
    };
    rmt_config(&ctx->rmt);
    rmt_driver_install(ctx->rmt.channel, 0, 0);
}

static void makeSendDataNEC(IRRC* ctx, uint8_t* data, int32_t length)
{
    configASSERT(length == 3);

    int32_t unit = ProtocolDef[ctx->protocol].unit;
    ctx->usedLen = 0;

    /* leader*/
    ctx->buff[ctx->usedLen] = (rmt_item32_t){
	.duration0 = 16 * unit,
	.level0 = 1,
	.duration1 = 8 * unit,
	.level1 = 0
    };
    ctx->usedLen++;

    /* customer & data */
    uint8_t bytes[4] = {
	data[0],
	data[1],
	data[2],
	~data[2]
    };
    for (int i = 0; i < sizeof(bytes); i++){
	for (int bit = 0; bit < 8; bit++){
	    uint8_t on = bytes[i] & (0x1 << bit);
	    ctx->buff[ctx->usedLen] = (rmt_item32_t){
		.duration0 = unit,
		.level0 = 1,
		.duration1 = unit * (on ? 3 : 1),
		.level1 = 0
	    };
	    ctx->usedLen++;
	}
    }

    /* stop bit */
    ctx->buff[ctx->usedLen] = (rmt_item32_t){
	.duration0 = unit,
	.level0 = 1,
	.duration1 = 16 * unit,
	.level1 = 0
    };
    ctx->usedLen++;
}

bool IRRCInit(IRRC* ctx, IRRC_MODE mode, IRRC_PROTOCOL protocol, int32_t gpio)
{
    configASSERT(mode == IRRC_TX);
    configASSERT(protocol == IRRC_NEC || protocol == IRRC_AEHA ||
		 protocol == IRRC_SONY);

    *ctx = (IRRC){
	.protocol = protocol,
	.mode = mode,
	.gpio = gpio,
	.buff = malloc(CMDBUFFLEN),
	.buffLen = CMDBUFFLEN,
	.usedLen = 0
    };

    if (ctx->buff){
	initTx(ctx, protocol, gpio);
	return true;
    }else{
	return false;
    }
}

void IRRCDeinit(IRRC* ctx)
{
    rmt_driver_uninstall(ctx->rmt.channel);
    free(ctx->buff);
    ctx->buff = NULL;
}

void IRRCSend(IRRC* ctx, uint8_t* data, int32_t length)
{
    if (ctx->protocol == IRRC_NEC){
	makeSendDataNEC(ctx, data, length);
	rmt_write_items(ctx->rmt.channel, ctx->buff, ctx->usedLen, 1);
	rmt_wait_tx_done(ctx->rmt.channel, portMAX_DELAY);
    }else{
	configASSERT(false);
    }
}
