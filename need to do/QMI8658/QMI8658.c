#include "..\..\..\User\Config.h"
#include "..\..\..\User\Task.h"
#include "..\..\..\Driver\inc\STC32G_I2C.h"
#include "QMI8658.h"
#include "QMI8658_port.h"
#include "Filter.h"

#if !QMI8658_DIAG_ENABLE
#undef LOGD
#define LOGD(tag, ...)
#endif

typedef struct
{
    QMI8658_State_t state;
    u8 data_ready;
    u8 init_retry;
    u8 selected_id;
    u8 last_status0;
    u8 last_statusint;
    u32 due_ms;
    u32 ready_deadline_ms;
} QMI8658_Context_t;

typedef struct
{
    u8 saw_reset_ready;
    u8 saw_status_nonzero;
    u8 saw_timestamp_nonzero;
    u8 saw_temp_nonzero;
    u8 saw_acc_nonzero;
    u8 saw_gyro_nonzero;
    u8 final_status0;
    u32 last_timestamp;
    int16 last_temp;
} QMI8658_DiagResult_t;

#define QMI8658_DIAG_CTRL7_ACC_ONLY            0x01U
#define QMI8658_DIAG_CTRL7_GYRO_ONLY           0x02U
#define QMI8658_DIAG_CTRL7_6DOF                0x03U
#define QMI8658_DIAG_ENABLE_DELAY_MS           30U
#define QMI8658_DIAG_WINDOW_MS                 300U
#define QMI8658_DIAG_SAMPLE_INTERVAL_MS        20U
#define QMI8658_DIAG_SAMPLE_COUNT              (QMI8658_DIAG_WINDOW_MS / QMI8658_DIAG_SAMPLE_INTERVAL_MS)
#define QMI8658_DIAG_RESET_SAMPLE_INTERVAL_MS  20U
#define QMI8658_DIAG_RESET_SAMPLE_COUNT        25U

u8 QMI8658_I2C_Addr = QMI8658_I2C_ADDR_PRIMARY;

static QMI8658_Context_t g_qmi8658_ctx = { QMI8658_STATE_IDLE, 0U, 0U, 0xFFU, 0U, 0U, 0UL, 0UL };
static u8 qmi8658_last_i2c_error = QMI8658_I2C_OK;

static u8 QMI8658_ReadReg(u8 reg_addr);
static u8 QMI8658_ReadRegAtAddr(u8 addr, u8 reg_addr, u8 *ok);
static u8 QMI8658_ReadNByte(u8 start_reg, u8 *buf, u8 len);
static u8 QMI8658_ReadNByteAtAddr(u8 addr, u8 start_reg, u8 *buf, u8 len);
static u8 QMI8658_WriteReg(u8 reg_addr, u8 reg_val);
static void QMI8658_SetState(QMI8658_State_t state, u16 delay_ms);
static u8 QMI8658_IsDue(u32 now_ms);
static char *QMI8658_StateName(QMI8658_State_t state);
static char *QMI8658_I2cErrName(u8 err);
static u8 QMI8658_SelectAddrByWhoAmI(u8 *selected_id);
static u8 QMI8658_ProbeAddr(u8 addr);
static u8 QMI8658_CheckReadyFlag(void);
static s8 QMI8658_ClearDataPath(void);
static u8 QMI8658_ConfigReadbackOk(void);
static void QMI8658_LogDataPath(char *phase);
static s8 QMI8658_EnterRetryOrFail(char *reason);
static s8 QMI8658_InitMinimalBlocking(void);
static void QMI8658_DiagResetResult(QMI8658_DiagResult_t *result);
static u8 QMI8658_DiagHasLiveData(QMI8658_DiagResult_t *result);
static u8 QMI8658_DiagReadSnapshot(char *phase, u16 sample_idx, QMI8658_DiagResult_t *result);
static u8 QMI8658_DiagCaptureResetWindow(QMI8658_DiagResult_t *result);
static u8 QMI8658_DiagConfigureLegacy(u8 ctrl7);
static char *QMI8658_DiagVerdict(QMI8658_DiagResult_t *result, u8 ctrl7, u8 force_soft_reset);
static u8 QMI8658_DiagRunExperiment(char *name, u8 ctrl7, u8 force_soft_reset, QMI8658_DiagResult_t *result);

static u8 QMI8658_ReadReg(u8 reg_addr)
{
    u8 value;

    value = 0xFFU;
    if (QMI8658_ReadNByte(reg_addr, &value, 1U) != 0U) {
        LOGE("IMU", "rd fail err=%s addr=0x%02X reg=0x%02X",
             QMI8658_I2cErrName(qmi8658_last_i2c_error),
             QMI8658_I2C_Addr,
             reg_addr);
        return 0xFFU;
    }
    return value;
}

static u8 QMI8658_ReadRegAtAddr(u8 addr, u8 reg_addr, u8 *ok)
{
    u8 value;

    value = 0xFFU;
    if (QMI8658_ReadNByteAtAddr(addr, reg_addr, &value, 1U) != 0U) {
        if (ok != NULL) {
            *ok = 0U;
        }
        return 0xFFU;
    }
    if (ok != NULL) {
        *ok = 1U;
    }
    return value;
}

static u8 QMI8658_ReadNByte(u8 start_reg, u8 *buf, u8 len)
{
    return QMI8658_ReadNByteAtAddr(QMI8658_I2C_Addr, start_reg, buf, len);
}

static u8 QMI8658_ReadNByteAtAddr(u8 addr, u8 start_reg, u8 *buf, u8 len)
{
    if (QMI8658Port_ReadN(addr, start_reg, buf, len, &qmi8658_last_i2c_error) != 0U) {
        return 1U;
    }
    qmi8658_last_i2c_error = QMI8658_I2C_OK;
    return 0U;
}

static u8 QMI8658_WriteReg(u8 reg_addr, u8 reg_val)
{
    if (QMI8658Port_WriteReg(QMI8658_I2C_Addr, reg_addr, reg_val, &qmi8658_last_i2c_error) != 0U) {
        return 1U;
    }
    qmi8658_last_i2c_error = QMI8658_I2C_OK;
    return 0U;
}

static void QMI8658_SetState(QMI8658_State_t state, u16 delay_ms)
{
    g_qmi8658_ctx.state = state;
    g_qmi8658_ctx.due_ms = Task_GetTickMs() + (u32)delay_ms;
    LOGD("IMU", "state -> %s delay=%u", QMI8658_StateName(state), (u16)delay_ms);
}

static u8 QMI8658_IsDue(u32 now_ms)
{
    return ((int32)(now_ms - g_qmi8658_ctx.due_ms) >= 0) ? 1U : 0U;
}

static char *QMI8658_StateName(QMI8658_State_t state)
{
    switch (state) {
    case QMI8658_STATE_IDLE:
        return "IDLE";
    case QMI8658_STATE_BUS_PREPARE:
        return "BUS_PREPARE";
    case QMI8658_STATE_PWR_WAIT:
        return "PWR_WAIT";
    case QMI8658_STATE_ID_PROBE:
        return "ID_PROBE";
    case QMI8658_STATE_QUIESCE:
        return "QUIESCE";
    case QMI8658_STATE_SOFT_RESET:
        return "SOFT_RESET";
    case QMI8658_STATE_RESET_WAIT:
        return "RESET_WAIT";
    case QMI8658_STATE_CLEAR_PATH:
        return "CLEAR_PATH";
    case QMI8658_STATE_CONFIG_WRITE:
        return "CONFIG_WRITE";
    case QMI8658_STATE_CONFIG_VERIFY:
        return "CONFIG_VERIFY";
    case QMI8658_STATE_ENABLE:
        return "ENABLE";
    case QMI8658_STATE_READY_WAIT:
        return "READY_WAIT";
    case QMI8658_STATE_READY:
        return "READY";
    default:
        return "FAILED";
    }
}

static char *QMI8658_I2cErrName(u8 err)
{
    switch (err) {
    case QMI8658_I2C_OK:
        return "OK";
    case QMI8658_I2C_ERR_BUSY:
        return "BUSY";
    case QMI8658_I2C_ERR_DEVW_NACK:
        return "DEVW_NACK";
    case QMI8658_I2C_ERR_REG_NACK:
        return "REG_NACK";
    case QMI8658_I2C_ERR_DEVR_NACK:
        return "DEVR_NACK";
    case QMI8658_I2C_ERR_DATA_NACK:
        return "DATA_NACK";
    default:
        return "PARAM";
    }
}

static u8 QMI8658_SelectAddrByWhoAmI(u8 *selected_id)
{
    u8 ok;
    u8 id;

    ok = 0U;
    id = QMI8658_ReadRegAtAddr(QMI8658_I2C_ADDR_PRIMARY, QMI8658_REG_WHO_AM_I, &ok);
    if ((ok != 0U) && (id == QMI8658_CHIP_ID_VALUE)) {
        QMI8658_I2C_Addr = QMI8658_I2C_ADDR_PRIMARY;
        if (selected_id != NULL) {
            *selected_id = id;
        }
        LOGI("IMU", "id probe primary ok id=0x%02X addr=0x%02X", id, QMI8658_I2C_Addr);
        return 0U;
    }

    ok = 0U;
    id = QMI8658_ReadRegAtAddr(QMI8658_I2C_ADDR_ALT, QMI8658_REG_WHO_AM_I, &ok);
    if ((ok != 0U) && (id == QMI8658_CHIP_ID_VALUE)) {
        QMI8658_I2C_Addr = QMI8658_I2C_ADDR_ALT;
        if (selected_id != NULL) {
            *selected_id = id;
        }
        LOGI("IMU", "id probe alt ok id=0x%02X addr=0x%02X", id, QMI8658_I2C_Addr);
        return 0U;
    }

    LOGW("IMU", "id probe fail err=%s", QMI8658_I2cErrName(qmi8658_last_i2c_error));
    return 1U;
}

static u8 QMI8658_ProbeAddr(u8 addr)
{
    u8 dummy;

    dummy = QMI8658_REG_WHO_AM_I;
    qmi8658_last_i2c_error = QMI8658_I2C_OK;
    I2C_WriteNbyte(QMI8658_I2C_WRITE(addr), QMI8658_REG_WHO_AM_I, &dummy, 0U);
    if (Get_MSBusy_Status() != 0U) {
        qmi8658_last_i2c_error = QMI8658_I2C_ERR_BUSY;
        return 1U;
    }
    return 0U;
}

static u8 QMI8658_CheckReadyFlag(void)
{
    u8 status0;
#if QMI8658_READY_MODE_STATUSINT
    u8 statusint;
#endif

    status0 = QMI8658_ReadReg(QMI8658_REG_STATUS0);
    if (qmi8658_last_i2c_error != QMI8658_I2C_OK) {
        return 0U;
    }
    g_qmi8658_ctx.last_status0 = status0;

#if QMI8658_READY_MODE_STATUSINT
    statusint = QMI8658_ReadReg(QMI8658_REG_STATUSINT);
    if (qmi8658_last_i2c_error != QMI8658_I2C_OK) {
        return 0U;
    }
    g_qmi8658_ctx.last_statusint = statusint;
    return ((statusint & QMI8658_STATUSINT_AVAIL) != 0U) ? 1U : 0U;
#else
    g_qmi8658_ctx.last_statusint = 0U;
#if !QMI8658_INIT_NONBLOCKING
    return ((status0 & QMI8658_STATUS0_A_DA) != 0U) ? 1U : 0U;
#else
    return (((status0 & (QMI8658_STATUS0_A_DA | QMI8658_STATUS0_G_DA)) ==
             (QMI8658_STATUS0_A_DA | QMI8658_STATUS0_G_DA)) ? 1U : 0U);
#endif
#endif
}

static s8 QMI8658_ClearDataPath(void)
{
    if (QMI8658_WriteReg(QMI8658_REG_CTRL7, 0x00U) != 0U) {
        return -1;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL6, QMI8658_CTRL6_INIT) != 0U) {
        return -1;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL8, QMI8658_CTRL8_INIT) != 0U) {
        return -1;
    }
    if (QMI8658_WriteReg(QMI8658_REG_FIFO_WTM, QMI8658_FIFO_WTM_INIT) != 0U) {
        return -1;
    }
    if (QMI8658_WriteReg(QMI8658_REG_FIFO_CTRL, QMI8658_FIFO_CTRL_BYPASS) != 0U) {
        return -1;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL9, QMI8658_CTRL9_CMD_RST_FIFO) != 0U) {
        return -1;
    }
    QMI8658Port_DelayMs(2U);
    if (QMI8658_WriteReg(QMI8658_REG_CTRL9, QMI8658_CTRL9_CMD_ACK) != 0U) {
        return -1;
    }
    return 0;
}

static u8 QMI8658_ConfigReadbackOk(void)
{
    u8 ctrl1;
    u8 ctrl2;
    u8 ctrl3;
    u8 ctrl5;
    u8 ctrl7;

    ctrl1 = QMI8658_ReadReg(QMI8658_REG_CTRL1);
    ctrl2 = QMI8658_ReadReg(QMI8658_REG_CTRL2);
    ctrl3 = QMI8658_ReadReg(QMI8658_REG_CTRL3);
    ctrl5 = QMI8658_ReadReg(QMI8658_REG_CTRL5);
    ctrl7 = QMI8658_ReadReg(QMI8658_REG_CTRL7);
#if QMI8658_DIAG_ENABLE
    LOGI("IMU", "cfg readback c1=%02X c2=%02X c3=%02X c5=%02X c7=%02X",
         ctrl1, ctrl2, ctrl3, ctrl5, ctrl7);
#endif

    if (qmi8658_last_i2c_error != QMI8658_I2C_OK) {
        return 0U;
    }
    if (ctrl1 != QMI8658_CTRL1_INIT) {
        return 0U;
    }
    if (ctrl2 != QMI8658_CTRL2_INIT) {
        return 0U;
    }
    if (ctrl3 != QMI8658_CTRL3_INIT) {
        return 0U;
    }
    if (ctrl5 != QMI8658_CTRL5_INIT) {
        return 0U;
    }
    if (ctrl7 != QMI8658_CTRL7_INIT) {
        return 0U;
    }
    return 1U;
}

static s8 QMI8658_InitMinimalBlocking(void)
{
    u8 id;
    u8 retry;
    u32 ready_deadline_ms;
    u32 now_ms;

    if (QMI8658Port_Init() != SUCCESS) {
        g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
        return -1;
    }

    id = 0xFFU;
    for (retry = 0U; retry < QMI8658_INIT_RETRY_MAX; retry++) {
        QMI8658Port_DelayMs(QMI8658_PWR_UP_DELAY_MS);
        if (QMI8658_SelectAddrByWhoAmI(&id) == 0U) {
            break;
        }
        QMI8658Port_DelayMs(QMI8658_INIT_RETRY_DELAY_MS);
    }

    if (retry >= QMI8658_INIT_RETRY_MAX) {
        g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
        return -1;
    }

    g_qmi8658_ctx.selected_id = id;

    if (QMI8658_WriteReg(QMI8658_REG_CTRL7, 0x00U) != 0U) {
        goto init_fail;
    }
    if (QMI8658_SOFT_RESET_ENABLE != 0) {
        if (QMI8658_WriteReg(QMI8658_REG_RESET, 0xB0U) != 0U) {
            goto init_fail;
        }
        QMI8658Port_DelayMs(QMI8658_RESET_DELAY_MS);
    }
    if (QMI8658_CLEAR_DATAPATH_ENABLE != 0) {
        if (QMI8658_ClearDataPath() != 0) {
            goto init_fail;
        }
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL1, QMI8658_CTRL1_INIT) != 0U) {
        goto init_fail;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL2, QMI8658_CTRL2_INIT) != 0U) {
        goto init_fail;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL3, QMI8658_CTRL3_INIT) != 0U) {
        goto init_fail;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL5, QMI8658_CTRL5_INIT) != 0U) {
        goto init_fail;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL7, QMI8658_CTRL7_INIT) != 0U) {
        goto init_fail;
    }

    QMI8658Port_DelayMs(QMI8658_ENABLE_DELAY_MS);
    if (QMI8658_ConfigReadbackOk() == 0U) {
        goto init_fail;
    }

    ready_deadline_ms = Task_GetTickMs() + (u32)QMI8658_READY_TIMEOUT_MS;
    while (1) {
        if (QMI8658_CheckReadyFlag() != 0U) {
            break;
        }
        now_ms = Task_GetTickMs();
        if ((int32)(now_ms - ready_deadline_ms) >= 0) {
            goto init_fail;
        }
        QMI8658Port_DelayMs(5U);
    }

    Filter_ResetGyroLowPass();
    g_qmi8658_ctx.state = QMI8658_STATE_READY;
    g_qmi8658_ctx.data_ready = 1U;
    return 0;

init_fail:
    (void)QMI8658_WriteReg(QMI8658_REG_CTRL7, 0x00U);
    g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
    g_qmi8658_ctx.data_ready = 0U;
    return -1;
}

static void QMI8658_LogDataPath(char *phase)
{
    u8 ctrl1;
    u8 ctrl2;
    u8 ctrl3;
    u8 ctrl5;
    u8 ctrl7;
    u8 status0;
    u8 statusint;
    u8 ts_raw[3];
    u8 temp_raw[2];
    u8 raw[12];
    u32 ts;
    int16 temp;
    int16 ax;
    int16 ay;
    int16 az;
    int16 gx;
    int16 gy;
    int16 gz;

    ctrl1 = QMI8658_ReadReg(QMI8658_REG_CTRL1);
    ctrl2 = QMI8658_ReadReg(QMI8658_REG_CTRL2);
    ctrl3 = QMI8658_ReadReg(QMI8658_REG_CTRL3);
    ctrl5 = QMI8658_ReadReg(QMI8658_REG_CTRL5);
    ctrl7 = QMI8658_ReadReg(QMI8658_REG_CTRL7);
    statusint = QMI8658_ReadReg(QMI8658_REG_STATUSINT);
    status0 = QMI8658_ReadReg(QMI8658_REG_STATUS0);
    LOGI("IMU", "%s regs c1=%02X c2=%02X c3=%02X c5=%02X c7=%02X",
         phase, ctrl1, ctrl2, ctrl3, ctrl5, ctrl7);
    LOGI("IMU", "%s status int=%02X s0=%02X", phase, statusint, status0);

    if (QMI8658_ReadNByte(QMI8658_REG_TIMESTAMP_L, ts_raw, 3U) == 0U) {
        ts = ((u32)ts_raw[2] << 16) | ((u32)ts_raw[1] << 8) | ts_raw[0];
        LOGI("IMU", "%s ts=%lu", phase, ts);
    }
    if (QMI8658_ReadNByte(QMI8658_REG_TEMP_L, temp_raw, 2U) == 0U) {
        temp = (int16)((u16)temp_raw[1] << 8 | temp_raw[0]);
        LOGI("IMU", "%s temp=%d", phase, temp);
    }
    if (QMI8658_ReadNByte(QMI8658_REG_AX_L, raw, 12U) == 0U) {
        ax = (int16)((u16)raw[1] << 8 | raw[0]);
        ay = (int16)((u16)raw[3] << 8 | raw[2]);
        az = (int16)((u16)raw[5] << 8 | raw[4]);
        gx = (int16)((u16)raw[7] << 8 | raw[6]);
        gy = (int16)((u16)raw[9] << 8 | raw[8]);
        gz = (int16)((u16)raw[11] << 8 | raw[10]);
        LOGI("IMU", "%s raw a=%d %d %d g=%d %d %d", phase, ax, ay, az, gx, gy, gz);
    }
}

static s8 QMI8658_EnterRetryOrFail(char *reason)
{
    if (g_qmi8658_ctx.init_retry < QMI8658_INIT_RETRY_MAX) {
        g_qmi8658_ctx.init_retry++;
        LOGW("IMU", "%s -> retry %u/%u", reason,
             (u16)g_qmi8658_ctx.init_retry,
             (u16)QMI8658_INIT_RETRY_MAX);
        QMI8658Port_BusRecover();
        QMI8658_SetState(QMI8658_STATE_BUS_PREPARE, QMI8658_INIT_RETRY_DELAY_MS);
        return 0;
    }

    LOGE("IMU", "%s -> FAILED err=%s", reason, QMI8658_I2cErrName(qmi8658_last_i2c_error));
    g_qmi8658_ctx.data_ready = 0U;
    QMI8658_SetState(QMI8658_STATE_FAILED, 0U);
    return -1;
}

static void QMI8658_DiagResetResult(QMI8658_DiagResult_t *result)
{
    if (result == NULL) {
        return;
    }

    result->saw_reset_ready = 0U;
    result->saw_status_nonzero = 0U;
    result->saw_timestamp_nonzero = 0U;
    result->saw_temp_nonzero = 0U;
    result->saw_acc_nonzero = 0U;
    result->saw_gyro_nonzero = 0U;
    result->final_status0 = 0U;
    result->last_timestamp = 0UL;
    result->last_temp = 0;
}

static u8 QMI8658_DiagHasLiveData(QMI8658_DiagResult_t *result)
{
    if (result == NULL) {
        return 0U;
    }

    return (((result->saw_status_nonzero != 0U) ||
             (result->saw_timestamp_nonzero != 0U) ||
             (result->saw_temp_nonzero != 0U) ||
             (result->saw_acc_nonzero != 0U) ||
             (result->saw_gyro_nonzero != 0U)) ? 1U : 0U);
}

static u8 QMI8658_DiagReadSnapshot(char *phase, u16 sample_idx, QMI8658_DiagResult_t *result)
{
    u8 statusint;
    u8 status0;
    u8 ts_raw[3];
    u8 temp_raw[2];
    u8 raw[12];
    u32 ts;
    int16 temp;
    int16 ax;
    int16 ay;
    int16 az;
    int16 gx;
    int16 gy;
    int16 gz;

    statusint = QMI8658_ReadReg(QMI8658_REG_STATUSINT);
    status0 = QMI8658_ReadReg(QMI8658_REG_STATUS0);
    if (qmi8658_last_i2c_error != QMI8658_I2C_OK) {
        return 1U;
    }
    if (QMI8658_ReadNByte(QMI8658_REG_TIMESTAMP_L, ts_raw, 3U) != 0U) {
        return 1U;
    }
    if (QMI8658_ReadNByte(QMI8658_REG_TEMP_L, temp_raw, 2U) != 0U) {
        return 1U;
    }
    if (QMI8658_ReadNByte(QMI8658_REG_AX_L, raw, 12U) != 0U) {
        return 1U;
    }

    ts = ((u32)ts_raw[2] << 16) | ((u32)ts_raw[1] << 8) | ts_raw[0];
    temp = (int16)((u16)temp_raw[1] << 8 | temp_raw[0]);
    ax = (int16)((u16)raw[1] << 8 | raw[0]);
    ay = (int16)((u16)raw[3] << 8 | raw[2]);
    az = (int16)((u16)raw[5] << 8 | raw[4]);
    gx = (int16)((u16)raw[7] << 8 | raw[6]);
    gy = (int16)((u16)raw[9] << 8 | raw[8]);
    gz = (int16)((u16)raw[11] << 8 | raw[10]);

    LOGI("IMU", "%s[%u] si=%02X s0=%02X ts=%lu t=%d a=%d %d %d g=%d %d %d",
         phase, sample_idx, statusint, status0, ts, temp, ax, ay, az, gx, gy, gz);

    g_qmi8658_ctx.last_statusint = statusint;
    g_qmi8658_ctx.last_status0 = status0;

    if (result != NULL) {
        if (status0 != 0U) {
            result->saw_status_nonzero = 1U;
        }
        if (ts != 0UL) {
            result->saw_timestamp_nonzero = 1U;
        }
        if (temp != 0) {
            result->saw_temp_nonzero = 1U;
        }
        if ((QMI8658_ACC_IS_ZERO(ax, ay, az) == 0) &&
            (QMI8658_DATA_IS_INVALID(ax, ay, az) == 0)) {
            result->saw_acc_nonzero = 1U;
        }
        if ((QMI8658_GYRO_IS_ZERO(gx, gy, gz) == 0) &&
            (QMI8658_DATA_IS_INVALID(gx, gy, gz) == 0)) {
            result->saw_gyro_nonzero = 1U;
        }
        result->final_status0 = status0;
        result->last_timestamp = ts;
        result->last_temp = temp;
    }

    return 0U;
}

static u8 QMI8658_DiagCaptureResetWindow(QMI8658_DiagResult_t *result)
{
    u8 i;
    u8 reset_state;

    for (i = 0U; i < QMI8658_DIAG_RESET_SAMPLE_COUNT; i++) {
        reset_state = QMI8658_ReadReg(QMI8658_REG_RESET_STATE);
        if (qmi8658_last_i2c_error != QMI8658_I2C_OK) {
            return 1U;
        }
        LOGI("IMU", "reset_probe[%u]=0x%02X", (u16)i, reset_state);
        if ((result != NULL) && (reset_state == QMI8658_RESET_STATE_READY)) {
            result->saw_reset_ready = 1U;
        }
        if ((u16)(i + 1U) < QMI8658_DIAG_RESET_SAMPLE_COUNT) {
            QMI8658Port_DelayMs(QMI8658_DIAG_RESET_SAMPLE_INTERVAL_MS);
        }
    }

    return 0U;
}

static u8 QMI8658_DiagConfigureLegacy(u8 ctrl7)
{
    u8 ctrl1_rb;
    u8 ctrl2_rb;
    u8 ctrl3_rb;
    u8 ctrl5_rb;
    u8 ctrl7_rb;

    LOGI("IMU", "diag cfg ctrl7=0x%02X", ctrl7);

    if (QMI8658_WriteReg(QMI8658_REG_CTRL7, 0x00U) != 0U) {
        return 1U;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL1, QMI8658_CTRL1_INIT) != 0U) {
        return 1U;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL2, QMI8658_CTRL2_INIT) != 0U) {
        return 1U;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL3, QMI8658_CTRL3_INIT) != 0U) {
        return 1U;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL5, QMI8658_CTRL5_INIT) != 0U) {
        return 1U;
    }
    if (QMI8658_WriteReg(QMI8658_REG_CTRL7, ctrl7) != 0U) {
        return 1U;
    }

    QMI8658Port_DelayMs(QMI8658_DIAG_ENABLE_DELAY_MS);

    ctrl1_rb = QMI8658_ReadReg(QMI8658_REG_CTRL1);
    ctrl2_rb = QMI8658_ReadReg(QMI8658_REG_CTRL2);
    ctrl3_rb = QMI8658_ReadReg(QMI8658_REG_CTRL3);
    ctrl5_rb = QMI8658_ReadReg(QMI8658_REG_CTRL5);
    ctrl7_rb = QMI8658_ReadReg(QMI8658_REG_CTRL7);
    LOGI("IMU", "diag readback c1=%02X c2=%02X c3=%02X c5=%02X c7=%02X",
         ctrl1_rb, ctrl2_rb, ctrl3_rb, ctrl5_rb, ctrl7_rb);
    if (qmi8658_last_i2c_error != QMI8658_I2C_OK) {
        return 1U;
    }

    return 0U;
}

static char *QMI8658_DiagVerdict(QMI8658_DiagResult_t *result, u8 ctrl7, u8 force_soft_reset)
{
    if (QMI8658_DiagHasLiveData(result) == 0U) {
        return "DATAPATH_DEAD";
    }
    if ((force_soft_reset != 0U) && (result->saw_reset_ready == 0U)) {
        return "RESET_FLAG_NEVER_80";
    }
    if ((ctrl7 == QMI8658_DIAG_CTRL7_ACC_ONLY) &&
        (result->saw_acc_nonzero != 0U) &&
        (result->saw_gyro_nonzero == 0U)) {
        return "ACC_ONLY_OK";
    }
    if ((ctrl7 == QMI8658_DIAG_CTRL7_GYRO_ONLY) &&
        (result->saw_gyro_nonzero != 0U) &&
        (result->saw_acc_nonzero == 0U)) {
        return "GYRO_ONLY_OK";
    }
    if (ctrl7 == QMI8658_DIAG_CTRL7_6DOF) {
        return "6DOF_ACTIVE";
    }
    return "PARTIAL_DATA";
}

static u8 QMI8658_DiagRunExperiment(char *name, u8 ctrl7, u8 force_soft_reset, QMI8658_DiagResult_t *result)
{
    u16 sample_idx;
    u8 reset_state;

    QMI8658_DiagResetResult(result);
    LOGI("IMU", "diag exp start name=%s ctrl7=0x%02X force_reset=%u",
         name, ctrl7, (u16)force_soft_reset);

    if (force_soft_reset != 0U) {
        if (QMI8658_WriteReg(QMI8658_REG_RESET, 0xB0U) != 0U) {
            LOGE("IMU", "%s soft reset WR fail", name);
            return 1U;
        }
        if (QMI8658_DiagCaptureResetWindow(result) != 0U) {
            LOGE("IMU", "%s reset probe fail", name);
            return 1U;
        }
    } else {
        reset_state = QMI8658_ReadReg(QMI8658_REG_RESET_STATE);
        if (qmi8658_last_i2c_error != QMI8658_I2C_OK) {
            LOGE("IMU", "%s reset_state pre-read fail", name);
            return 1U;
        }
        LOGI("IMU", "%s reset_state pre=0x%02X", name, reset_state);
    }

    if (QMI8658_DiagConfigureLegacy(ctrl7) != 0U) {
        LOGE("IMU", "%s configure fail", name);
        return 1U;
    }

    for (sample_idx = 0U; sample_idx < QMI8658_DIAG_SAMPLE_COUNT; sample_idx++) {
        if (QMI8658_DiagReadSnapshot(name, sample_idx, result) != 0U) {
            LOGE("IMU", "%s snapshot fail idx=%u", name, sample_idx);
            return 1U;
        }
        if ((u16)(sample_idx + 1U) < QMI8658_DIAG_SAMPLE_COUNT) {
            QMI8658Port_DelayMs(QMI8658_DIAG_SAMPLE_INTERVAL_MS);
        }
    }

    LOGI("IMU",
         "diag exp done name=%s verdict=%s reset80=%u s0=%u ts=%u temp=%u acc=%u gyro=%u last_s0=%02X last_ts=%lu last_t=%d",
         name,
         QMI8658_DiagVerdict(result, ctrl7, force_soft_reset),
         (u16)result->saw_reset_ready,
         (u16)result->saw_status_nonzero,
         (u16)result->saw_timestamp_nonzero,
         (u16)result->saw_temp_nonzero,
         (u16)result->saw_acc_nonzero,
         (u16)result->saw_gyro_nonzero,
         result->final_status0,
         result->last_timestamp,
         result->last_temp);
    return 0U;
}

s8 QMI8658_Init(void)
{
#if QMI8658_INIT_NONBLOCKING
    QMI8658_RequestReinit();
    return 0;
#else
#if !QMI8658_DIAG_ENABLE
    g_qmi8658_ctx.data_ready = 0U;
    g_qmi8658_ctx.init_retry = 0U;
    g_qmi8658_ctx.selected_id = 0xFFU;
    g_qmi8658_ctx.last_status0 = 0U;
    g_qmi8658_ctx.last_statusint = 0U;
    g_qmi8658_ctx.state = QMI8658_STATE_IDLE;
    qmi8658_last_i2c_error = QMI8658_I2C_OK;
    return QMI8658_InitMinimalBlocking();
#else
    u8 id;
    u8 retry;
    u8 baseline_ok;
    u8 reset_6dof_ok;
    u8 acc_ok;
    u8 gyro_ok;
    QMI8658_DiagResult_t baseline_result;
    QMI8658_DiagResult_t softreset_result;
    QMI8658_DiagResult_t acc_result;
    QMI8658_DiagResult_t gyro_result;

    id = 0xFFU;
    baseline_ok = 0U;
    reset_6dof_ok = 0U;
    acc_ok = 0U;
    gyro_ok = 0U;
    g_qmi8658_ctx.data_ready = 0U;
    g_qmi8658_ctx.init_retry = 0U;
    g_qmi8658_ctx.selected_id = 0xFFU;
    g_qmi8658_ctx.last_status0 = 0U;
    g_qmi8658_ctx.last_statusint = 0U;
    g_qmi8658_ctx.state = QMI8658_STATE_IDLE;
    qmi8658_last_i2c_error = QMI8658_I2C_OK;

    LOGI("IMU", "========== QMI8658 Init Start ==========");
    LOGI("IMU", "legacy bring-up: soft_reset=%u diag=%u bus=%s",
         (u16)QMI8658_SOFT_RESET_ENABLE,
         (u16)QMI8658_DIAG_ENABLE,
         QMI8658Port_BackendName());
    LOGD("IMU", "primary addr=0x%02X alt=0x%02X",
         QMI8658_I2C_WRITE(QMI8658_I2C_ADDR_PRIMARY),
         QMI8658_I2C_WRITE(QMI8658_I2C_ADDR_ALT));

    if (QMI8658Port_Init() != SUCCESS) {
        LOGE("IMU", "port init fail");
        g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
        return -1;
    }

    for (retry = 0U; retry < 3U; retry++) {
        QMI8658_I2C_Addr = QMI8658_I2C_ADDR_PRIMARY;
        LOGD("IMU", "probe primary 0x%02X try=%u",
             QMI8658_I2C_WRITE(QMI8658_I2C_Addr), (u16)(retry + 1U));
        if (QMI8658_ProbeAddr(QMI8658_I2C_ADDR_PRIMARY) == 0U) {
            LOGD("IMU", "found device at primary addr=0x%02X",
                 QMI8658_I2C_WRITE(QMI8658_I2C_Addr));
            break;
        }

        LOGW("IMU", "primary addr no ACK, try alt");
        QMI8658_I2C_Addr = QMI8658_I2C_ADDR_ALT;
        LOGD("IMU", "probe alt 0x%02X", QMI8658_I2C_WRITE(QMI8658_I2C_Addr));
        if (QMI8658_ProbeAddr(QMI8658_I2C_ADDR_ALT) == 0U) {
            LOGD("IMU", "found device at alt addr=0x%02X",
                 QMI8658_I2C_WRITE(QMI8658_I2C_Addr));
            break;
        }

        QMI8658_I2C_Addr = QMI8658_I2C_ADDR_PRIMARY;
        LOGW("IMU", "addr probe fail try=%u, wait 200ms retry", (u16)(retry + 1U));
        QMI8658Port_DelayMs(200U);
    }

    if (retry >= 3U) {
        LOGE("IMU", "no device found after 3 tries");
        g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
        return -1;
    }

    LOGD("IMU", "power-up wait %ums", (u16)QMI8658_PWR_UP_DELAY_MS);
    QMI8658Port_DelayMs(QMI8658_PWR_UP_DELAY_MS);

    id = QMI8658_ReadID();
    if (id != QMI8658_CHIP_ID_VALUE) {
        LOGE("IMU", "WHO_AM_I error got=0x%02X exp=0x%02X", id, QMI8658_CHIP_ID_VALUE);
        g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
        return -1;
    }
    g_qmi8658_ctx.selected_id = id;
    LOGD("IMU", "WHO_AM_I=0x%02X OK addr=0x%02X", id, QMI8658_I2C_Addr);

    if (QMI8658_DiagRunExperiment("baseline", QMI8658_DIAG_CTRL7_6DOF, 0U, &baseline_result) != 0U) {
        g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
        return -1;
    }
    if (QMI8658_DiagRunExperiment("softreset", QMI8658_DIAG_CTRL7_6DOF, 1U, &softreset_result) != 0U) {
        g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
        return -1;
    }
    if (QMI8658_DiagRunExperiment("acc_only", QMI8658_DIAG_CTRL7_ACC_ONLY, 0U, &acc_result) != 0U) {
        g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
        return -1;
    }
    if (QMI8658_DiagRunExperiment("gyro_only", QMI8658_DIAG_CTRL7_GYRO_ONLY, 0U, &gyro_result) != 0U) {
        g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
        return -1;
    }

    baseline_ok = QMI8658_DiagHasLiveData(&baseline_result);
    reset_6dof_ok = QMI8658_DiagHasLiveData(&softreset_result);
    acc_ok = QMI8658_DiagHasLiveData(&acc_result);
    gyro_ok = QMI8658_DiagHasLiveData(&gyro_result);

    if (softreset_result.saw_reset_ready == 0U) {
        LOGW("IMU", "diag verdict: soft reset path never observed reg0x4D=0x80");
    }
    if ((acc_result.saw_acc_nonzero != 0U) && (gyro_result.saw_gyro_nonzero == 0U)) {
        LOGW("IMU", "diag verdict: accelerometer path alive but gyroscope path inactive");
    }
    if ((baseline_ok == 0U) && ((acc_ok != 0U) || (gyro_ok != 0U))) {
        LOGW("IMU", "diag verdict: single-sensor mode alive while 6DOF path is blocked");
    }
    if ((baseline_ok == 0U) && (reset_6dof_ok == 0U) && (acc_ok == 0U) && (gyro_ok == 0U)) {
        LOGE("IMU", "diag verdict: all experiments stayed zero, prefer hardware/chip root cause");
    }

    Filter_ResetGyroLowPass();
    LOGI("IMU", "========== QMI8658 Init Done ==========");
    LOGI("IMU", "diag summary baseline=%u soft6dof=%u acc=%u gyro=%u",
         (u16)baseline_ok, (u16)reset_6dof_ok, (u16)acc_ok, (u16)gyro_ok);

    if ((baseline_ok != 0U) || (reset_6dof_ok != 0U)) {
        if (QMI8658_DiagConfigureLegacy(QMI8658_DIAG_CTRL7_6DOF) != 0U) {
            LOGE("IMU", "restore 6dof config fail");
            g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
            g_qmi8658_ctx.data_ready = 0U;
            return -1;
        }
        g_qmi8658_ctx.last_status0 = QMI8658_ReadReg(QMI8658_REG_STATUS0);
        g_qmi8658_ctx.state = QMI8658_STATE_READY;
        g_qmi8658_ctx.data_ready = 1U;
        return 0;
    }

    (void)QMI8658_WriteReg(QMI8658_REG_CTRL7, 0x00U);
    g_qmi8658_ctx.state = QMI8658_STATE_FAILED;
    g_qmi8658_ctx.data_ready = 0U;
    return -1;
#endif
#endif
}

s8 QMI8658_Service(void)
{
    u8 id;
    u8 reset_state;
    u32 now_ms;

    now_ms = Task_GetTickMs();

#if !QMI8658_INIT_NONBLOCKING
    if (g_qmi8658_ctx.state == QMI8658_STATE_READY) {
        (void)QMI8658_PollDataReady();
        return 0;
    }
    if (g_qmi8658_ctx.state == QMI8658_STATE_FAILED) {
        return -1;
    }
    return 0;
#endif

    if (g_qmi8658_ctx.state == QMI8658_STATE_IDLE) {
        return 0;
    }

    if (g_qmi8658_ctx.state == QMI8658_STATE_READY) {
        (void)QMI8658_PollDataReady();
        return 0;
    }

    if (g_qmi8658_ctx.state == QMI8658_STATE_FAILED) {
        return -1;
    }

    if (QMI8658_IsDue(now_ms) == 0U) {
        return 0;
    }

    switch (g_qmi8658_ctx.state) {
    case QMI8658_STATE_BUS_PREPARE:
        LOGI("IMU", "========== QMI8658 Init Start ==========");
        LOGI("IMU", "bring-up: soft_reset=%u diag=%u bus=%s",
             (u16)QMI8658_SOFT_RESET_ENABLE,
             (u16)QMI8658_DIAG_ENABLE,
             QMI8658Port_BackendName());
        if (QMI8658Port_Init() != SUCCESS) {
            return QMI8658_EnterRetryOrFail("port init fail");
        }
        if (QMI8658Port_BusNeedsRecover() != 0U) {
            QMI8658Port_BusRecover();
        }
        QMI8658_SetState(QMI8658_STATE_PWR_WAIT, QMI8658_PWR_UP_DELAY_MS);
        return 0;

    case QMI8658_STATE_PWR_WAIT:
        QMI8658_SetState(QMI8658_STATE_ID_PROBE, 0U);
        return 0;

    case QMI8658_STATE_ID_PROBE:
        if (QMI8658_SelectAddrByWhoAmI(&id) != 0U) {
            return QMI8658_EnterRetryOrFail("who_am_i fail");
        }
        g_qmi8658_ctx.selected_id = id;
        LOGI("IMU", "WHO_AM_I=0x%02X i2c_addr=0x%02X", id, QMI8658_I2C_Addr);
        QMI8658_SetState(QMI8658_STATE_QUIESCE, 0U);
        return 0;

    case QMI8658_STATE_QUIESCE:
        if (QMI8658_WriteReg(QMI8658_REG_CTRL7, 0x00U) != 0U) {
            return QMI8658_EnterRetryOrFail("quiesce fail");
        }
        if (QMI8658_CLEAR_DATAPATH_ENABLE != 0) {
            if (QMI8658_ClearDataPath() != 0) {
                return QMI8658_EnterRetryOrFail("pre-clear fail");
            }
        }
        QMI8658_SetState(QMI8658_STATE_SOFT_RESET, 0U);
        return 0;

    case QMI8658_STATE_SOFT_RESET:
        if (QMI8658_SOFT_RESET_ENABLE != 0) {
            if (QMI8658_WriteReg(QMI8658_REG_RESET, 0xB0U) != 0U) {
                return QMI8658_EnterRetryOrFail("soft reset fail");
            }
            QMI8658_SetState(QMI8658_STATE_RESET_WAIT, QMI8658_RESET_DELAY_MS);
            return 0;
        }
        QMI8658_SetState(QMI8658_STATE_CLEAR_PATH, 0U);
        return 0;

    case QMI8658_STATE_RESET_WAIT:
        reset_state = QMI8658_ReadReg(QMI8658_REG_RESET_STATE);
        LOGI("IMU", "reset_state=0x%02X", reset_state);
        if (qmi8658_last_i2c_error != QMI8658_I2C_OK) {
            return QMI8658_EnterRetryOrFail("reset_state read fail");
        }
        QMI8658_SetState(QMI8658_STATE_CLEAR_PATH, 0U);
        return 0;

    case QMI8658_STATE_CLEAR_PATH:
        if (QMI8658_CLEAR_DATAPATH_ENABLE != 0) {
            if (QMI8658_ClearDataPath() != 0) {
                return QMI8658_EnterRetryOrFail("clear path fail");
            }
        }
#if QMI8658_DIAG_ENABLE
        QMI8658_LogDataPath("after_clear");
#endif
        QMI8658_SetState(QMI8658_STATE_CONFIG_WRITE, 0U);
        return 0;

    case QMI8658_STATE_CONFIG_WRITE:
        if (QMI8658_WriteReg(QMI8658_REG_CTRL7, 0x00U) != 0U) {
            return QMI8658_EnterRetryOrFail("cfg ctrl7 disable fail");
        }
        if (QMI8658_WriteReg(QMI8658_REG_CTRL1, QMI8658_CTRL1_INIT) != 0U) {
            return QMI8658_EnterRetryOrFail("cfg ctrl1 fail");
        }
        if (QMI8658_WriteReg(QMI8658_REG_CTRL2, QMI8658_CTRL2_INIT) != 0U) {
            return QMI8658_EnterRetryOrFail("cfg ctrl2 fail");
        }
        if (QMI8658_WriteReg(QMI8658_REG_CTRL3, QMI8658_CTRL3_INIT) != 0U) {
            return QMI8658_EnterRetryOrFail("cfg ctrl3 fail");
        }
        if (QMI8658_WriteReg(QMI8658_REG_CTRL5, QMI8658_CTRL5_INIT) != 0U) {
            return QMI8658_EnterRetryOrFail("cfg ctrl5 fail");
        }
        QMI8658_SetState(QMI8658_STATE_ENABLE, 0U);
        return 0;

    case QMI8658_STATE_ENABLE:
        if (QMI8658_WriteReg(QMI8658_REG_CTRL7, QMI8658_CTRL7_INIT) != 0U) {
            return QMI8658_EnterRetryOrFail("enable fail");
        }
#if QMI8658_DIAG_ENABLE
        QMI8658_LogDataPath("after_enable");
#endif
        g_qmi8658_ctx.ready_deadline_ms = now_ms + (u32)QMI8658_READY_TIMEOUT_MS;
        QMI8658_SetState(QMI8658_STATE_CONFIG_VERIFY, QMI8658_ENABLE_DELAY_MS);
        return 0;

    case QMI8658_STATE_CONFIG_VERIFY:
        if (QMI8658_ConfigReadbackOk() == 0U) {
            return QMI8658_EnterRetryOrFail("cfg verify fail");
        }
        QMI8658_SetState(QMI8658_STATE_READY_WAIT, 0U);
        return 0;

    case QMI8658_STATE_READY_WAIT:
        if (QMI8658_CheckReadyFlag() != 0U) {
            g_qmi8658_ctx.data_ready = 1U;
            Filter_ResetGyroLowPass();
            LOGI("IMU", "ready addr=0x%02X id=0x%02X status0=0x%02X",
                 QMI8658_I2C_Addr,
                 g_qmi8658_ctx.selected_id,
                 g_qmi8658_ctx.last_status0);
            QMI8658_SetState(QMI8658_STATE_READY, 0U);
            return 0;
        }
        if ((int32)(now_ms - g_qmi8658_ctx.ready_deadline_ms) >= 0) {
#if QMI8658_DIAG_ENABLE
            QMI8658_LogDataPath("ready_timeout");
#endif
            return QMI8658_EnterRetryOrFail("ready timeout");
        }
        g_qmi8658_ctx.due_ms = now_ms + 5U;
        return 0;

    default:
        QMI8658_SetState(QMI8658_STATE_FAILED, 0U);
        return -1;
    }
}

void QMI8658_RequestReinit(void)
{
    g_qmi8658_ctx.data_ready = 0U;
    g_qmi8658_ctx.init_retry = 0U;
    g_qmi8658_ctx.selected_id = 0xFFU;
    g_qmi8658_ctx.last_status0 = 0U;
    g_qmi8658_ctx.last_statusint = 0U;
    qmi8658_last_i2c_error = QMI8658_I2C_OK;
    QMI8658_I2C_Addr = QMI8658_I2C_ADDR_PRIMARY;
    QMI8658_SetState(QMI8658_STATE_BUS_PREPARE, 0U);
}

u8 QMI8658_IsReady(void)
{
    return (g_qmi8658_ctx.state == QMI8658_STATE_READY) ? 1U : 0U;
}

QMI8658_State_t QMI8658_GetState(void)
{
    return g_qmi8658_ctx.state;
}

u8 QMI8658_HasDataReady(void)
{
    return g_qmi8658_ctx.data_ready;
}

void QMI8658_ClearDataReady(void)
{
    g_qmi8658_ctx.data_ready = 0U;
}

u8 QMI8658_PollDataReady(void)
{
    if (g_qmi8658_ctx.state != QMI8658_STATE_READY) {
        return 0U;
    }

    if (QMI8658_CheckReadyFlag() != 0U) {
        g_qmi8658_ctx.data_ready = 1U;
    }
    return g_qmi8658_ctx.data_ready;
}

u8 QMI8658_ReadID(void)
{
    return QMI8658_ReadReg(QMI8658_REG_WHO_AM_I);
}

s8 QMI8658_ReadAcc(int16 *x, int16 *y, int16 *z)
{
    u8 raw[6];
    int16 ax;
    int16 ay;
    int16 az;

    if ((x == NULL) || (y == NULL) || (z == NULL)) {
        qmi8658_last_i2c_error = QMI8658_I2C_ERR_PARAM;
        return -1;
    }
    if (QMI8658_ReadNByte(QMI8658_REG_AX_L, raw, 6U) != 0U) {
        return -1;
    }

    ax = (int16)((u16)raw[1] << 8 | raw[0]);
    ay = (int16)((u16)raw[3] << 8 | raw[2]);
    az = (int16)((u16)raw[5] << 8 | raw[4]);
    if (QMI8658_ACC_IS_ZERO(ax, ay, az) || QMI8658_DATA_IS_INVALID(ax, ay, az)) {
        return -1;
    }

    *x = ax;
    *y = ay;
    *z = az;
    return 0;
}

s8 QMI8658_ReadGyro(int16 *x, int16 *y, int16 *z)
{
    u8 raw[6];
    int16 gx;
    int16 gy;
    int16 gz;

    if ((x == NULL) || (y == NULL) || (z == NULL)) {
        qmi8658_last_i2c_error = QMI8658_I2C_ERR_PARAM;
        return -1;
    }
    if (QMI8658_ReadNByte(QMI8658_REG_GX_L, raw, 6U) != 0U) {
        return -1;
    }

    gx = (int16)((u16)raw[1] << 8 | raw[0]);
    gy = (int16)((u16)raw[3] << 8 | raw[2]);
    gz = (int16)((u16)raw[5] << 8 | raw[4]);
    if (QMI8658_GYRO_IS_ZERO(gx, gy, gz) || QMI8658_DATA_IS_INVALID(gx, gy, gz)) {
        return -1;
    }

    *x = gx;
    *y = gy;
    *z = gz;
    return 0;
}

s8 QMI8658_ReadGyroFiltered(int16 *x, int16 *y, int16 *z)
{
    int16 gx;
    int16 gy;
    int16 gz;

    if ((x == NULL) || (y == NULL) || (z == NULL)) {
        qmi8658_last_i2c_error = QMI8658_I2C_ERR_PARAM;
        return -1;
    }
    if (QMI8658_ReadGyro(&gx, &gy, &gz) != 0) {
        return -1;
    }

    return Filter_GyroLowPass(gx, gy, gz, x, y, z);
}

s8 QMI8658_ReadTemp(int16 *temp)
{
    u8 raw[2];

    if (temp == NULL) {
        qmi8658_last_i2c_error = QMI8658_I2C_ERR_PARAM;
        return -1;
    }
    if (QMI8658_ReadNByte(QMI8658_REG_TEMP_L, raw, 2U) != 0U) {
        return -1;
    }

    *temp = (int16)((u16)raw[1] << 8 | raw[0]);
    return 0;
}

s8 QMI8658_ReadAll(int16 *ax, int16 *ay, int16 *az,
                   int16 *gx, int16 *gy, int16 *gz)
{
    u8 raw[12];
    int16 aax;
    int16 aay;
    int16 aaz;
    int16 ggx;
    int16 ggy;
    int16 ggz;

    if ((ax == NULL) || (ay == NULL) || (az == NULL) ||
        (gx == NULL) || (gy == NULL) || (gz == NULL)) {
        qmi8658_last_i2c_error = QMI8658_I2C_ERR_PARAM;
        return -1;
    }
    if (QMI8658_ReadNByte(QMI8658_REG_AX_L, raw, 12U) != 0U) {
        return -1;
    }

    aax = (int16)((u16)raw[1] << 8 | raw[0]);
    aay = (int16)((u16)raw[3] << 8 | raw[2]);
    aaz = (int16)((u16)raw[5] << 8 | raw[4]);
    ggx = (int16)((u16)raw[7] << 8 | raw[6]);
    ggy = (int16)((u16)raw[9] << 8 | raw[8]);
    ggz = (int16)((u16)raw[11] << 8 | raw[10]);

    if ((QMI8658_ACC_IS_ZERO(aax, aay, aaz) || QMI8658_DATA_IS_INVALID(aax, aay, aaz)) &&
        (QMI8658_GYRO_IS_ZERO(ggx, ggy, ggz) || QMI8658_DATA_IS_INVALID(ggx, ggy, ggz))) {
        return -1;
    }

    *ax = aax;
    *ay = aay;
    *az = aaz;
    *gx = ggx;
    *gy = ggy;
    *gz = ggz;
    g_qmi8658_ctx.data_ready = 0U;
    return 0;
}

s8 QMI8658_Wait_AccReady(u16 timeout_ms)
{
    u16 i;

    for (i = 0U; i < timeout_ms; i += 5U) {
        if ((QMI8658_ReadReg(QMI8658_REG_STATUS0) & QMI8658_STATUS0_A_DA) != 0U) {
            return 0;
        }
        QMI8658Port_DelayMs(5U);
    }
    return -1;
}

s8 QMI8658_Wait_GyroReady(u16 timeout_ms)
{
    u16 i;

    for (i = 0U; i < timeout_ms; i += 5U) {
        if ((QMI8658_ReadReg(QMI8658_REG_STATUS0) & QMI8658_STATUS0_G_DA) != 0U) {
            return 0;
        }
        QMI8658Port_DelayMs(5U);
    }
    return -1;
}

void QMI8658_BusRecover(void)
{
    QMI8658Port_BusRecover();
}

s8 QMI8658_Enable(void)
{
    return (QMI8658_WriteReg(QMI8658_REG_CTRL7, QMI8658_CTRL7_INIT) == 0U) ? 0 : -1;
}

s8 QMI8658_Disable(void)
{
    return (QMI8658_WriteReg(QMI8658_REG_CTRL7, 0x00U) == 0U) ? 0 : -1;
}

void QMI8658_DumpRawRegs(void)
{
    QMI8658_LogDataPath("dump");
}

u8 QMI8658_GetLastI2cError(void)
{
    return qmi8658_last_i2c_error;
}

char *QMI8658_GetLastI2cErrorName(void)
{
    return QMI8658_I2cErrName(qmi8658_last_i2c_error);
}
