#include "KCT8206.h"

/* RXEN 和 TXEN 是必需控制脚；ANT_SEL 和回读脚可选。 */
static int8 KCT8206_CheckDevice(kct8206_t *dev)
{
    if ((dev == 0) ||
        (dev->bus.set_rxen == 0) ||
        (dev->bus.set_txen == 0)) {
        return KCT8206_ERR_PARAM;
    }

    return KCT8206_OK;
}

static void KCT8206_DelayUs(kct8206_t *dev, u16 us)
{
    if ((dev != 0) && (dev->bus.delay_us != 0)) {
        dev->bus.delay_us(dev->bus.ctx, us);
    }
}

static void KCT8206_SetRxEn(kct8206_t *dev, u8 level)
{
    dev->bus.set_rxen(dev->bus.ctx, level);
}

static void KCT8206_SetTxEn(kct8206_t *dev, u8 level)
{
    dev->bus.set_txen(dev->bus.ctx, level);
}

static void KCT8206_SetAntSel(kct8206_t *dev, u8 antenna)
{
    if (dev->bus.set_ant_sel != 0) {
        dev->bus.set_ant_sel(dev->bus.ctx, (antenna == KCT8206_ANT2) ? 1U : 0U);
    }
}

int8 KCT8206_Bind(kct8206_t *dev, const kct8206_bus_t *bus)
{
    if ((dev == 0) ||
        (bus == 0) ||
        (bus->set_rxen == 0) ||
        (bus->set_txen == 0)) {
        return KCT8206_ERR_PARAM;
    }

    /* 绑定后先进入明确的安全前端状态。 */
    dev->bus = *bus;
    dev->state = KCT8206_STATE_IDLE;
    dev->antenna = KCT8206_ANT1;
    KCT8206_SetTxEn(dev, 0U);
    KCT8206_SetRxEn(dev, 0U);
    KCT8206_SetAntSel(dev, KCT8206_ANT1);

    return KCT8206_OK;
}

int8 KCT8206_SetAntenna(kct8206_t *dev, u8 antenna)
{
    int8 ret;

    ret = KCT8206_CheckDevice(dev);
    if (ret != KCT8206_OK) {
        return ret;
    }
    if ((antenna != KCT8206_ANT1) && (antenna != KCT8206_ANT2)) {
        return KCT8206_ERR_PARAM;
    }

    dev->antenna = antenna;
    KCT8206_SetAntSel(dev, antenna);
    return KCT8206_OK;
}

int8 KCT8206_EnterIdle(kct8206_t *dev)
{
    int8 ret;

    ret = KCT8206_CheckDevice(dev);
    if (ret != KCT8206_OK) {
        return ret;
    }

    KCT8206_SetTxEn(dev, 0U);
    KCT8206_SetRxEn(dev, 0U);
    dev->state = KCT8206_STATE_IDLE;
    return KCT8206_OK;
}

int8 KCT8206_EnterRx(kct8206_t *dev)
{
    int8 ret;

    ret = KCT8206_CheckDevice(dev);
    if (ret != KCT8206_OK) {
        return ret;
    }

    /* RX 路径：关闭 PA/TX，打开 LNA/RX。 */
    KCT8206_SetTxEn(dev, 0U);
    KCT8206_SetRxEn(dev, 1U);
    KCT8206_DelayUs(dev, 5U);
    dev->state = KCT8206_STATE_RX;
    return KCT8206_OK;
}

int8 KCT8206_PrepareLegacyTx(kct8206_t *dev)
{
    int8 ret;

    ret = KCT8206_CheckDevice(dev);
    if (ret != KCT8206_OK) {
        return ret;
    }

    /* 旧 TX 时序：保持 RXEN 为高，再制造干净的 TXEN 上升沿。 */
    KCT8206_SetRxEn(dev, 1U);
    KCT8206_SetTxEn(dev, 0U);
    KCT8206_DelayUs(dev, 5U);
    return KCT8206_OK;
}

int8 KCT8206_EnterTx(kct8206_t *dev)
{
    int8 ret;

    ret = KCT8206_PrepareLegacyTx(dev);
    if (ret != KCT8206_OK) {
        return ret;
    }

    /* PrepareLegacyTx 后通过 TXEN 上升沿打开 TX 路径。 */
    KCT8206_SetTxEn(dev, 1U);
    KCT8206_DelayUs(dev, 5U);
    dev->state = KCT8206_STATE_TX;
    return KCT8206_OK;
}

int8 KCT8206_ReadStatus(kct8206_t *dev, kct8206_status_t *status)
{
    int8 ret;

    ret = KCT8206_CheckDevice(dev);
    if (ret != KCT8206_OK) {
        return ret;
    }
    if (status == 0) {
        return KCT8206_ERR_PARAM;
    }

    status->state = dev->state;
    status->antenna = dev->antenna;
    status->rxen = (dev->bus.get_rxen != 0) ? dev->bus.get_rxen(dev->bus.ctx) : 0U;
    status->txen = (dev->bus.get_txen != 0) ? dev->bus.get_txen(dev->bus.ctx) : 0U;
    status->ant_sel = (dev->bus.get_ant_sel != 0) ? dev->bus.get_ant_sel(dev->bus.ctx) : dev->antenna;

    return KCT8206_OK;
}
