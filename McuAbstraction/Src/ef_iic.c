#include "ef_iic.h"
#include "STC32G_DMA.h"
#include "STC32G_GPIO.h"
#include "STC32G_I2C.h"
#include "STC32G_NVIC.h"
#include "STC32G_Switch.h"

/*
 * STC32G 官方 APP_DMA_I2C 示例采用硬件 IIC + IIC DMA：
 * - TX DMA 源缓冲格式：[dev_w][reg][data0..dataN-1]
 * - RX DMA 前置命令：dev_w -> reg -> repeated-start dev_r
 * - RX DMA 只负责收 data0..dataN-1
 *
 * 本文件保留该硬件路径，但把裸寄存器、DMA 标志和命令字封在 MCU 抽象层。
 * 上层只看到 7-bit 地址 + 8-bit 寄存器地址 + 连续数据，不直接接触官方 SDK。
 */

/* TX 最多发送：设备地址 1 字节 + 寄存器地址 1 字节 + payload 255 字节。 */
#define EF_IIC_TX_BUF_LEN        257U
#define EF_IIC_RX_BUF_LEN        EF_IIC_DMA_MAX_LEN

/* I2CMSST bit6 = MSIF，主机命令完成标志；DMA_I2C_CR bit2 = ACKERR。 */
#define EF_IIC_MSIF_MASK         0x40U
#define EF_IIC_ACKERR_MASK       0x04U

/*
 * DMA 只能稳定访问 xdata 区域，因此收发缓冲固定放在 xdata。
 * 这里不做多实例，符合当前 STC32G 只有一组硬件 IIC 的使用方式。
 */
static u8 xdata ef_iic_tx_buf[EF_IIC_TX_BUF_LEN];
static u8 xdata ef_iic_rx_buf[EF_IIC_RX_BUF_LEN];
static u16 ef_iic_timeout = EF_IIC_DEFAULT_TIMEOUT;
static u8 ef_iic_ready = 0U;

static u16 ef_iic_get_timeout(void)
{
    /* 允许调用者传 0 使用默认值，避免因为未初始化字段导致立即超时。 */
    return (ef_iic_timeout == 0U) ? EF_IIC_DEFAULT_TIMEOUT : ef_iic_timeout;
}

static int8 ef_iic_wait_master_idle(void)
{
    u16 timeout;

    /* 事务开始前必须等主机状态机空闲，否则新的 START/STOP 命令会被打断。 */
    timeout = ef_iic_get_timeout();
    while (Get_MSBusy_Status() != 0U) {
        if (timeout == 0U) {
            return EF_IIC_ERR_BUSY;
        }
        timeout--;
    }

    return EF_IIC_OK;
}

static int8 ef_iic_wait_done(void)
{
    u16 timeout;

    /*
     * STC IIC 主机每执行一个 I2CMSCR 命令后置 I2CMSST.MSIF。
     * 轮询完成后必须手动清 MSIF，否则下一条短命令会误判为已完成。
     */
    timeout = ef_iic_get_timeout();
    while ((I2CMSST & EF_IIC_MSIF_MASK) == 0U) {
        if (timeout == 0U) {
            return EF_IIC_ERR_TIMEOUT;
        }
        timeout--;
    }

    I2CMSST &= (u8)(~EF_IIC_MSIF_MASK);
    return EF_IIC_OK;
}

static int8 ef_iic_send_cmd_data(u8 cmd, u8 data)
{
    /*
     * 用于 DMA 读之前的短命令阶段，对应官方示例 SendCmdData()。
     * cmd 直接使用官方命令字：0x09 = START+SEND+ACK，0x0A = SEND+ACK。
     */
    I2CTXD = data;
    I2CMSCR = cmd;
    return ef_iic_wait_done();
}

static int8 ef_iic_check_ack_error(void)
{
    /*
     * ACKERR 由硬件/DMA 在地址或数据阶段未收到 ACK 时置位。
     * 读取后立即清除，避免污染下一次事务的错误判断。
     */
    if ((DMA_I2C_CR & EF_IIC_ACKERR_MASK) != 0U) {
        DMA_I2C_CR &= (u8)(~EF_IIC_ACKERR_MASK);
        return EF_IIC_ERR_NACK;
    }

    return EF_IIC_OK;
}

static int8 ef_iic_wait_dma_tx_done(void)
{
    u16 timeout;

    /* DmaI2CTFlag 在 DMA I2CT 中断里清零；这里保留超时兜底，避免死等。 */
    timeout = ef_iic_get_timeout();
    while (DmaI2CTFlag != 0) {
        if (timeout == 0U) {
            DmaI2CTFlag = 0;
            I2C_DMA_Disable();
            return EF_IIC_ERR_TIMEOUT;
        }
        timeout--;
    }

    return EF_IIC_OK;
}

static int8 ef_iic_wait_dma_rx_done(void)
{
    u16 timeout;

    /* DmaI2CRFlag 在 DMA I2CR 中断里清零；超时时关闭 IIC DMA 并交给上层重试。 */
    timeout = ef_iic_get_timeout();
    while (DmaI2CRFlag != 0) {
        if (timeout == 0U) {
            DmaI2CRFlag = 0;
            I2C_DMA_Disable();
            return EF_IIC_ERR_TIMEOUT;
        }
        timeout--;
    }

    return EF_IIC_OK;
}

static void ef_iic_config_pins(u8 pin_group)
{
    EAXSFR();

    /*
     * 硬件 IIC 线按开漏配置。P3.3/P3.2 的宏名顺序来自 STC 官方 I2C_P33_P32。
     */
    if (pin_group == EF_IIC_PIN_P14_P15) {
        P1_MODE_OUT_OD(GPIO_Pin_4 | GPIO_Pin_5);
        P1_PULL_UP_ENABLE(GPIO_Pin_4 | GPIO_Pin_5);
        I2C_SW(I2C_P14_P15);
    } else if (pin_group == EF_IIC_PIN_P24_P25) {
        P2_MODE_OUT_OD(GPIO_Pin_4 | GPIO_Pin_5);
        P2_PULL_UP_ENABLE(GPIO_Pin_4 | GPIO_Pin_5);
        I2C_SW(I2C_P24_P25);
    } else if (pin_group == EF_IIC_PIN_P76_P77) {
        P7_MODE_OUT_OD(GPIO_Pin_6 | GPIO_Pin_7);
        P7_PULL_UP_ENABLE(GPIO_Pin_6 | GPIO_Pin_7);
        I2C_SW(I2C_P76_P77);
    } else {
        P3_MODE_OUT_OD(GPIO_Pin_2 | GPIO_Pin_3);
        P3_PULL_UP_ENABLE(GPIO_Pin_2 | GPIO_Pin_3);
        I2C_SW(I2C_P33_P32);
    }
}

static u8 ef_iic_pin_group_is_valid(u8 pin_group)
{
    /* 当前只允许官方硬件 IIC 支持的四组复用脚。 */
    return (pin_group <= EF_IIC_PIN_P33_P32) ? 1U : 0U;
}

int8 ef_iic_init(const ef_iic_config_t *config)
{
    I2C_InitTypeDef i2c_init;
    DMA_I2C_InitTypeDef dma_init;

    if (config == 0) {
        return EF_IIC_ERR_PARAM;
    }
    if (ef_iic_pin_group_is_valid(config->pin_group) == 0U) {
        return EF_IIC_ERR_PARAM;
    }

    ef_iic_ready = 0U;
    ef_iic_timeout = (config->timeout == 0U) ? EF_IIC_DEFAULT_TIMEOUT : config->timeout;

    /* 引脚先切到目标复用，随后再打开 IIC 功能，避免初始化期间误产生总线动作。 */
    ef_iic_config_pins(config->pin_group);

    I2C_Function(DISABLE);
    I2CMSST = 0x00U;
    I2CMSCR = 0x00U;

    /* 主机模式不启用 IIC 中断，DMA 完成由 DMA I2CT/I2CR 中断清标志。 */
    i2c_init.I2C_Mode = I2C_Mode_Master;
    i2c_init.I2C_Enable = ENABLE;
    i2c_init.I2C_MS_WDTA = DISABLE;
    i2c_init.I2C_Speed = config->speed;
    I2C_Init(&i2c_init);
    NVIC_I2C_Init(I2C_Mode_Master, DISABLE, Priority_0);

    /*
     * STC DMA 长度寄存器采用“寄存器值 + 1 = 实际传输字节数”的习惯。
     * 这里初始化为最大容量，单次事务前再通过 SET_I2CT/R_DMA_LEN() 缩小长度。
     */
    dma_init.DMA_TX_Length = EF_IIC_DMA_MAX_LEN + 1U;
    dma_init.DMA_TX_Buffer = (u16)ef_iic_tx_buf;
    dma_init.DMA_RX_Length = EF_IIC_DMA_MAX_LEN - 1U;
    dma_init.DMA_RX_Buffer = (u16)ef_iic_rx_buf;
    dma_init.DMA_TX_Enable = ENABLE;
    dma_init.DMA_RX_Enable = ENABLE;
    DMA_I2C_Inilize(&dma_init);

    NVIC_DMA_I2CT_Init(ENABLE, Priority_0, Priority_0);
    NVIC_DMA_I2CR_Init(ENABLE, Priority_0, Priority_0);
    DMA_I2CR_CLRFIFO();
    I2C_DMA_Disable();

    ef_iic_ready = 1U;
    return EF_IIC_OK;
}

int8 ef_iic_write_regs(u8 dev_addr, u8 reg_addr, const u8 *data, u8 len)
{
    u8 i;
    int8 ret;

    if (ef_iic_ready == 0U) {
        return EF_IIC_ERR_NOT_INIT;
    }
    if ((data == 0) || (len == 0U)) {
        return EF_IIC_ERR_PARAM;
    }
    if (len > EF_IIC_DMA_MAX_LEN) {
        return EF_IIC_ERR_LENGTH;
    }

    ret = ef_iic_wait_master_idle();
    if (ret != EF_IIC_OK) {
        return ret;
    }

    /* 每次事务开始前清 ACKERR，确保本次返回值只反映本次 IIC 访问。 */
    DMA_I2C_CR &= (u8)(~EF_IIC_ACKERR_MASK);

    /*
     * 0x89: START + DMA SEND + ACK check，沿用官方 DMA IIC 示例命令字。
     * dev_addr 传入 7-bit 地址，这里左移后形成写地址字节。
     */
    ef_iic_tx_buf[0] = (u8)(dev_addr << 1);
    ef_iic_tx_buf[1] = reg_addr;
    for (i = 0U; i < len; i++) {
        ef_iic_tx_buf[(u16)i + 2U] = data[i];
    }

    DmaI2CTFlag = 1;
    I2C_MSCMD(0x89U);
    I2C_DMA_Enable();
    /*
     * 写事务实际发送 dev_w + reg + len 个数据字节。
     * 官方示例对 `number` 个数据字节配置 `number + 1`，硬件按 +1 规则完成
     * 总计 `number + 2` 字节发送，这里保持同样写法。
     */
    SET_I2CT_DMA_LEN((u16)len + 1U);
    SET_I2C_DMA_ST((u16)len + 1U);
    DMA_I2CT_TRIG();

    ret = ef_iic_wait_dma_tx_done();
    I2C_DMA_Disable();
    if (ret != EF_IIC_OK) {
        return ret;
    }
    return ef_iic_check_ack_error();
}

int8 ef_iic_read_regs(u8 dev_addr, u8 reg_addr, u8 *data, u8 len)
{
    u8 i;
    int8 ret;

    if (ef_iic_ready == 0U) {
        return EF_IIC_ERR_NOT_INIT;
    }
    if ((data == 0) || (len == 0U)) {
        return EF_IIC_ERR_PARAM;
    }
    if (len > EF_IIC_DMA_MAX_LEN) {
        return EF_IIC_ERR_LENGTH;
    }

    ret = ef_iic_wait_master_idle();
    if (ret != EF_IIC_OK) {
        return ret;
    }

    /* RX 事务前先关 DMA，短命令阶段只通过 I2CTXD/I2CMSCR 发送地址和寄存器。 */
    I2C_DMA_Disable();
    DMA_I2C_CR &= (u8)(~EF_IIC_ACKERR_MASK);

    /*
     * 读事务前置阶段：
     * 0x09 发送 START + 数据 + ACK 检查。
     * 0x0A 发送数据 + ACK 检查。
     */
    ret = ef_iic_send_cmd_data(0x09U, (u8)(dev_addr << 1));
    if (ret != EF_IIC_OK) {
        return ret;
    }
    ret = ef_iic_check_ack_error();
    if (ret != EF_IIC_OK) {
        return ret;
    }
    ret = ef_iic_send_cmd_data(0x0AU, reg_addr);
    if (ret != EF_IIC_OK) {
        return ret;
    }
    ret = ef_iic_check_ack_error();
    if (ret != EF_IIC_OK) {
        return ret;
    }
    ret = ef_iic_send_cmd_data(0x09U, (u8)((dev_addr << 1) | 0x01U));
    if (ret != EF_IIC_OK) {
        return ret;
    }
    ret = ef_iic_check_ack_error();
    if (ret != EF_IIC_OK) {
        return ret;
    }

    DmaI2CRFlag = 1;
    /*
     * 0x8B: START + DMA RECEIVE + ACK/NAK 自动处理，沿用官方示例命令字。
     * len=1 时配置 0，正好对应 STC DMA 的“寄存器值 + 1”计数规则。
     */
    I2C_MSCMD(0x8BU);
    I2C_DMA_Enable();
    SET_I2CR_DMA_LEN((u16)len - 1U);
    SET_I2C_DMA_ST((u16)len - 1U);
    DMA_I2CR_TRIG();

    ret = ef_iic_wait_dma_rx_done();
    I2C_DMA_Disable();
    if (ret != EF_IIC_OK) {
        return ret;
    }
    ret = ef_iic_check_ack_error();
    if (ret != EF_IIC_OK) {
        return ret;
    }

    /* RX DMA 收到的数据先落到 xdata 缓冲，再复制到调用者提供的普通指针。 */
    for (i = 0U; i < len; i++) {
        data[i] = ef_iic_rx_buf[i];
    }

    return EF_IIC_OK;
}

u8 ef_iic_is_busy(void)
{
    return (Get_MSBusy_Status() != 0U) ? 1U : 0U;
}
