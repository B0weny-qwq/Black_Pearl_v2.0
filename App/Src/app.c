#include "app.h"
#include "board_console.h"
#include "board_gps.h"
#include "board_imu.h"
#include "board_mag.h"
#include "board_motor.h"
#include "board_power.h"
#include "board_spi_ps.h"
#include "board_wireless.h"
#include "platform_scheduler.h"
#include "AHRS.h"
#include "Filter.h"
#include "HeadingEstimator.h"
#include "logger.h"
#include "MagCompass.h"
#include "ship_protocol.h"

#define APP_AHRS_LOG_DECIMATION          32U
#define APP_MAG_COMPASS_STATIC_SETTLE_MS 3000UL
#define APP_SPI_PS_RX_EVENT_MAX          32U

static HeadingEstimator_t app_heading;
static u8 app_heading_ready = 0U;
static u16 app_heading_deg100 = 0U;
static int16 app_heading_rel_deg100 = 0;
static u8 app_ahrs_started = 0U;
static u8 app_spi_ps_ready = 0U;

static const char *app_ship_event_name(ship_protocol_event_type_t type)
{
	switch(type)
	{
	case SHIP_PROTOCOL_EVENT_THROTTLE:
		return "throttle";
	case SHIP_PROTOCOL_EVENT_KEY_EDGE:
		return "key-edge";
	case SHIP_PROTOCOL_EVENT_RETURN_HOME:
		return "return-home";
	case SHIP_PROTOCOL_EVENT_FISH_POINT:
		return "fish-point";
	case SHIP_PROTOCOL_EVENT_RETURN_SWITCH:
		return "return-switch";
	case SHIP_PROTOCOL_EVENT_KEY_ACTION:
		return "key-action";
	case SHIP_PROTOCOL_EVENT_POWER_SAMPLE:
		return "power-sample";
	case SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED:
		return "power-level";
	case SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED:
		return "low-power";
	case SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX:
		return "spi-ps-rx";
	case SHIP_PROTOCOL_EVENT_FRAME_ERROR:
		return "frame-error";
	default:
		return "none";
	}
}

static u32 app_abs32(int32 value)
{
	if(value < 0L)
	{
		return (u32)(-value);
	}
	return (u32)value;
}

static int16 app_wrap_signed_deg100(int32 angle)
{
	while(angle >= 18000L)
	{
		angle -= 36000L;
	}
	while(angle < -18000L)
	{
		angle += 36000L;
	}
	return (int16)angle;
}

static u16 app_wrap_heading_deg100(int32 angle)
{
	while(angle >= 36000L)
	{
		angle -= 36000L;
	}
	while(angle < 0L)
	{
		angle += 36000L;
	}
	return (u16)angle;
}

static int32 app_float_to_deg100(float value)
{
	float scaled;

	scaled = value * 100.0f;
	if(scaled >= 0.0f)
	{
		return (int32)(scaled + 0.5f);
	}
	return (int32)(scaled - 0.5f);
}

static u8 app_ahrs_is_static(const AHRS_State_t *att)
{
	if(att == 0)
	{
		return 0U;
	}
	if((att->flags & AHRS_FLAG_GYRO_BIAS_READY) == 0U)
	{
		return 0U;
	}
	if((att->flags & AHRS_FLAG_ACC_VALID) == 0U)
	{
		return 0U;
	}
	if(app_abs32((int32)att->gyro_x_dps100) > AHRS_GYRO_STILL_DPS100)
	{
		return 0U;
	}
	if(app_abs32((int32)att->gyro_y_dps100) > AHRS_GYRO_STILL_DPS100)
	{
		return 0U;
	}
	if(app_abs32((int32)att->gyro_z_dps100) > AHRS_GYRO_STILL_DPS100)
	{
		return 0U;
	}
	return 1U;
}

static void app_ahrs_reset(void)
{
	AHRS_Reset();
	Heading_Init(&app_heading);
	MagCompass_Reset();
	Filter_ResetMagLowPass();
	Filter_ResetGyroLowPass();
	app_heading_ready = 0U;
	app_heading_deg100 = 0U;
	app_heading_rel_deg100 = 0;
	app_ahrs_started = 1U;
}

static void app_log_imu_diag(void)
{
	board_imu_diag_t imu_diag;

	if(board_imu_get_diag(&imu_diag) != BOARD_IMU_OK)
	{
		return;
	}

	LOGE("IMU", "diag chip_rc=%d addr=0x%02X who=0x%02X s0=0x%02X",
		 imu_diag.chip_error,
		 (u16)imu_diag.i2c_addr,
		 (u16)imu_diag.who_am_i,
		 (u16)imu_diag.status0);
	LOGE("IMU", "diag c1=0x%02X c2=0x%02X c3=0x%02X c5=0x%02X c7=0x%02X",
		 (u16)imu_diag.ctrl1,
		 (u16)imu_diag.ctrl2,
		 (u16)imu_diag.ctrl3,
		 (u16)imu_diag.ctrl5,
		 (u16)imu_diag.ctrl7);
	LOGE("IMU", "cfg retry=%u reg=0x%02X wr=0x%02X rb=0x%02X ret=%d",
		 (u16)imu_diag.cfg_retry,
		 (u16)imu_diag.cfg_reg,
		 (u16)imu_diag.cfg_write,
		 (u16)imu_diag.cfg_read,
		 imu_diag.cfg_ret);
	LOGE("IMU", "i2c op=%u stage=%u ret=%d b0=0x%02X b1=0x%02X rec=%d msst=0x%02X mscr=0x%02X",
		 (u16)imu_diag.i2c_op,
		 (u16)imu_diag.i2c_stage,
		 imu_diag.i2c_ret,
		 (u16)imu_diag.i2c_state_before,
		 (u16)imu_diag.i2c_state_after,
		 imu_diag.i2c_recover_ret,
		 (u16)imu_diag.i2c_msst,
		 (u16)imu_diag.i2c_mscr);
}

static void app_try_imu_after_bringup(void)
{
	int8 ret;

	ret = board_imu_init();
	if(ret == BOARD_IMU_OK)
	{
		LOGI("IMU", "QMI8658 init ok");
	}
	else
	{
		LOGE("IMU", "QMI8658 init fail rc=%d", ret);
		app_log_imu_diag();
	}
}

static void app_read_imu_once(void)
{
	int8 ret;
	board_imu_sample_t imu_sample;

	if(board_imu_is_ready() == 0U)
	{
		return;
	}

	ret = board_imu_read(&imu_sample);
	if(ret == BOARD_IMU_OK)
	{
		LOGI("IMU", "sample a=%d,%d,%d g=%d,%d,%d t=%d",
			 imu_sample.acc_x_raw,
			 imu_sample.acc_y_raw,
			 imu_sample.acc_z_raw,
			 imu_sample.gyro_x_raw,
			 imu_sample.gyro_y_raw,
			 imu_sample.gyro_z_raw,
			 imu_sample.temp_raw);
	}
	else
	{
		LOGW("IMU", "sample not ready rc=%d", ret);
	}
}

static void app_bring_up_devices(void)
{
	int8 ret;

	LOGI("APP", "Black Pearl v2.0");
	LOGI("APP", "v1.0 port/decoupled baseline");

	ret = board_gps_init();
	if(ret == BOARD_GPS_OK)
	{
		LOGI("GPS", "init ok ready=%u", (u16)board_gps_is_ready());
	}
	else
	{
		LOGE("GPS", "init fail rc=%d", ret);
	}

	app_try_imu_after_bringup();

	ret = board_mag_init();
	if(ret == BOARD_MAG_OK)
	{
		LOGI("MAG", "QMC6309 init ok");
	}
	else
	{
		LOGE("MAG", "QMC6309 init fail rc=%d", ret);
	}

	ret = board_power_init();
	if(ret == BOARD_POWER_OK)
	{
		LOGI("POWER", "init ok");
#if BOARD_POWER_BAT_MV_UNCALIBRATED
		LOGW("POWER", "bat_mv uncalibrated, using adc_mv scale");
#endif
	}
	else
	{
		LOGE("POWER", "init fail rc=%d", ret);
	}

	ret = board_wireless_init();
	if(ret == BOARD_WIRELESS_OK)
	{
		LOGI("WL", "init ok");
		ship_protocol_init();
	}
	else
	{
		LOGE("WL", "init fail rc=%d", ret);
	}

	ret = board_spi_ps_init();
	if(ret == BOARD_SPI_PS_OK)
	{
		app_spi_ps_ready = 1U;
		LOGI("SPI-PS", "init ok");
	}
	else
	{
		app_spi_ps_ready = 0U;
		if(ret == BOARD_SPI_PS_ERR_RESOURCE)
		{
			LOGW("SPI-PS", "disabled: shared SPI resource with LT8920");
		}
	}

    ret = board_motor_init();
    if(ret == BOARD_MOTOR_OK)
	{
		LOGI("MOTOR", "init ok");
	}
	else
	{
		LOGE("MOTOR", "init fail rc=%d", ret);
	}

	app_read_imu_once();
	app_ahrs_reset();
}

static void app_ahrs_log(const AHRS_State_t *att,
						 const mag_compass_state_t *mag_state,
						 u8 heading_static,
						 u8 heading_mag_settled)
{
	int32 heading_err_cd;
	int32 heading_pred_cd;

	if(att == 0)
	{
		return;
	}

	heading_err_cd = app_float_to_deg100(app_heading.heading_err_deg);
	heading_pred_cd = app_float_to_deg100(app_heading.heading_pred_deg);
	LOGI("AHRS", "rpy=%d,%d,%d gy=%d,%d,%d flg=0x%02X",
		 att->roll_deg100,
		 att->pitch_deg100,
		 att->yaw_deg100,
		 att->gyro_x_dps100,
		 att->gyro_y_dps100,
		 att->gyro_z_dps100,
		 (u16)att->flags);
	LOGI("HDG", "abs=%u rel=%d mag=%u rdy=%u st=%u set=%u err=%ld pred=%ld",
		 app_heading_deg100,
		 app_heading_rel_deg100,
		 (mag_state != 0) ? mag_state->heading_deg100 : 0U,
		 (mag_state != 0) ? (u16)mag_state->ready : 0U,
		 (u16)heading_static,
		 (u16)heading_mag_settled,
		 heading_err_cd,
		 heading_pred_cd);
}

static void app_ahrs_poll(void)
{
	static u8 timing_started = 0U;
	static u32 last_imu_ms = 0UL;
	static u32 last_mag_ms = 0UL;
	static u32 heading_static_start_ms = 0UL;
	static u16 sample_div = 0U;
	static u8 read_error_latched = 0U;
	static u8 heading_seeded = 0U;
	static u8 last_heading_static = 0U;
	u32 now_ms;
	u32 elapsed_ms;
	u16 dt_ms;
	u16 stable_mag_heading_cd;
	u8 stable_mag_valid;
	u8 heading_static;
	u8 heading_mag_settled;
	board_imu_sample_t imu_sample;
	board_mag_sample_t mag_sample;
	const AHRS_State_t *att;
	const mag_compass_state_t *mag_state;
	int16 mag_x;
	int16 mag_y;
	int16 mag_z;
	float heading_seed_deg;

	if((board_imu_is_ready() == 0U) || (app_ahrs_started == 0U))
	{
		return;
	}

	now_ms = platform_scheduler_get_tick_ms();
	if(timing_started == 0U)
	{
		last_imu_ms = now_ms;
		last_mag_ms = now_ms;
		timing_started = 1U;
		return;
	}

	elapsed_ms = now_ms - last_imu_ms;
	if(elapsed_ms < AHRS_IMU_PERIOD_MS)
	{
		return;
	}
	last_imu_ms = now_ms;
	dt_ms = (elapsed_ms > AHRS_DT_MAX_MS) ? AHRS_DT_MAX_MS : (u16)elapsed_ms;

	(void)board_imu_service();
	if(board_imu_read(&imu_sample) != BOARD_IMU_OK)
	{
		if(read_error_latched == 0U)
		{
			LOGW("AHRS", "imu read fail state=%u", (u16)board_imu_get_state());
			read_error_latched = 1U;
		}
		return;
	}
	read_error_latched = 0U;

	if(AHRS_UpdateRaw6Axis(imu_sample.acc_x_raw,
						   imu_sample.acc_y_raw,
						   imu_sample.acc_z_raw,
						   imu_sample.gyro_x_raw,
						   imu_sample.gyro_y_raw,
						   imu_sample.gyro_z_raw,
						   dt_ms) != 0)
	{
		return;
	}

	att = AHRS_GetState();
	heading_static = 0U;
	heading_mag_settled = 0U;
	if((att->flags & AHRS_FLAG_READY) != 0U)
	{
		heading_static = app_ahrs_is_static(att);
		if((heading_static != 0U) && (last_heading_static == 0U))
		{
			heading_static_start_ms = now_ms;
			MagCompass_Reset();
		}
		if(heading_static == 0U)
		{
			heading_static_start_ms = 0UL;
		}
		else if((now_ms - heading_static_start_ms) >= APP_MAG_COMPASS_STATIC_SETTLE_MS)
		{
			heading_mag_settled = 1U;
		}
		last_heading_static = heading_static;
	}
	else
	{
		heading_seeded = 0U;
		last_heading_static = 0U;
		heading_static_start_ms = 0UL;
		app_heading_ready = 0U;
		Heading_Init(&app_heading);
	}

	stable_mag_valid = 0U;
	mag_state = MagCompass_GetState();
	stable_mag_heading_cd = (mag_state != 0) ? mag_state->heading_deg100 : 0U;
	if((board_mag_is_ready() != 0U) &&
	   ((now_ms - last_mag_ms) >= AHRS_MAG_PERIOD_MS))
	{
		last_mag_ms = now_ms;
		if(board_mag_read(&mag_sample) == BOARD_MAG_OK)
		{
			if(Filter_MagLowPass(mag_sample.mag_x_raw,
								 mag_sample.mag_y_raw,
								 mag_sample.mag_z_raw,
								 &mag_x,
								 &mag_y,
								 &mag_z) == 0)
			{
				(void)AHRS_UpdateRawMag(mag_x, mag_y, mag_z);
				if((heading_mag_settled != 0U) &&
				   (MagCompass_Update(mag_x, mag_y, mag_z, &stable_mag_heading_cd) != 0U))
				{
					stable_mag_valid = 1U;
				}
			}
		}
	}

	if((att->flags & AHRS_FLAG_READY) == 0U)
	{
		return;
	}

	if(heading_seeded == 0U)
	{
		mag_state = MagCompass_GetState();
		if((mag_state == 0) || (mag_state->ready == 0U) || (heading_mag_settled == 0U))
		{
			app_heading_ready = 0U;
			return;
		}
		heading_seed_deg = (float)mag_state->heading_deg100 * 0.01f;
		Heading_SetHeadingDeg(&app_heading, heading_seed_deg);
		Heading_ResetZero(&app_heading);
		heading_seeded = 1U;
		app_heading_ready = 1U;
	}

	Heading_Update(&app_heading,
				   (float)att->gyro_z_dps100 * 0.01f,
				   (float)stable_mag_heading_cd * 0.01f,
				   stable_mag_valid,
				   heading_static,
				   (float)dt_ms * 0.001f);
	app_heading_deg100 = app_wrap_heading_deg100(Heading_GetDeg100(&app_heading));
	app_heading_rel_deg100 = app_wrap_signed_deg100(Heading_GetRelativeDeg100(&app_heading));
	app_heading_ready = 1U;

	if(sample_div < APP_AHRS_LOG_DECIMATION)
	{
		sample_div++;
		return;
	}
	sample_div = 0U;
	app_ahrs_log(att, MagCompass_GetState(), heading_static, heading_mag_settled);
}

static void app_spi_ps_poll(void)
{
	u8 buffer[APP_SPI_PS_RX_EVENT_MAX];
	u8 len;
	int8 ret;

	if(app_spi_ps_ready == 0U)
	{
		return;
	}
	if(board_spi_ps_service() == 0U)
	{
		return;
	}

	len = 0U;
	ret = board_spi_ps_read(buffer, APP_SPI_PS_RX_EVENT_MAX, &len);
	if((ret == BOARD_SPI_PS_OK) || (ret == BOARD_SPI_PS_ERR_OVERFLOW))
	{
		ship_protocol_publish_spi_ps_frame(buffer, len, ret);
	}
}

static void app_dispatch_ship_event(const ship_protocol_event_snapshot_t *event)
{
	if((event == 0) || (event->type == SHIP_PROTOCOL_EVENT_NONE))
	{
		return;
	}

	switch(event->type)
	{
	case SHIP_PROTOCOL_EVENT_KEY_EDGE:
		LOGI("EVT", "ship %s seq=%u key=0x%02X input=%d/%d",
			 app_ship_event_name(event->type),
			 event->sequence,
			 (u16)event->throttle.key_event,
			 event->throttle.throttle_input,
			 event->throttle.steering_input);
		break;
	case SHIP_PROTOCOL_EVENT_KEY_ACTION:
		LOGI("EVT", "ship %s seq=%u action=%u key=0x%02X",
			 app_ship_event_name(event->type),
			 event->sequence,
			 (u16)event->key_action,
			 (u16)event->throttle.key_event);
		break;
	case SHIP_PROTOCOL_EVENT_RETURN_HOME:
	case SHIP_PROTOCOL_EVENT_FISH_POINT:
	case SHIP_PROTOCOL_EVENT_RETURN_SWITCH:
		LOGI("EVT", "ship %s seq=%u cmd=0x%02X point=%u switch=0x%02X",
			 app_ship_event_name(event->type),
			 event->sequence,
			 (u16)event->cmd,
			 (u16)event->point_valid,
			 (u16)event->switch_state);
		break;
	case SHIP_PROTOCOL_EVENT_POWER_LEVEL_CHANGED:
	case SHIP_PROTOCOL_EVENT_LOW_POWER_LATCHED:
		LOGI("EVT", "ship %s seq=%u level=%u bat=%lu valid=%u",
			 app_ship_event_name(event->type),
			 event->sequence,
			 (u16)event->power.level,
			 event->power.bat_mv,
			 (u16)event->power.valid);
		break;
	case SHIP_PROTOCOL_EVENT_SPI_PS_FRAME_RX:
		LOGI("EVT", "ship %s seq=%u rc=%d len=%u stored=%u",
			 app_ship_event_name(event->type),
			 event->sequence,
			 event->spi_ps.status,
			 (u16)event->spi_ps.len,
			 (u16)event->spi_ps.stored_len);
		break;
	case SHIP_PROTOCOL_EVENT_FRAME_ERROR:
		LOGW("EVT", "ship %s seq=%u cmd=0x%02X len=%u",
			 app_ship_event_name(event->type),
			 event->sequence,
			 (u16)event->cmd,
			 (u16)event->payload_len);
		break;
	default:
		break;
	}
}

static void app_ship_event_poll(void)
{
	ship_protocol_event_snapshot_t event;

	while(ship_protocol_take_event(&event) != 0U)
	{
		app_dispatch_ship_event(&event);
	}
}

void app_init(void)
{
	if(board_console_init() == BOARD_CONSOLE_OK)
	{
		log_init();
	}

	app_bring_up_devices();
}

void app_loop(void)
{
	board_gps_poll();
	(void)board_wireless_poll();
	ship_protocol_run_scheduler();
	(void)board_wireless_search_signal_poll();
	app_spi_ps_poll();
	app_ship_event_poll();
	app_ahrs_poll();
	(void)board_motor_service();
}

const AHRS_State_t *app_get_attitude_state(void)
{
	return AHRS_GetState();
}

u8 app_get_heading_ready(void)
{
	return app_heading_ready;
}

u16 app_get_heading_deg100(void)
{
	return app_heading_deg100;
}

int16 app_get_heading_relative_deg100(void)
{
	return app_heading_rel_deg100;
}
