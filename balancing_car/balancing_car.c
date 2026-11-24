/*****************************************************************/ /**
* @file balancing_car.c
* @brief UniRTOS (EG800Z-CN / EC718) two-wheel self-balancing car, "stand up" only.
*
*  Ported from WHEELTEC B570_streamline (STM32F103):
*    - Kalman filter angle (Way_Angle = 2), same constants
*    - Balance PD + Velocity PI, same gains / scaling (Kp,Kd x100)
*    - 5 ms control period, driven by the MPU6050 data-ready INT
*
*  Pin map (module PIN numbers):
*    MPU6050 SCL  PIN57   | MPU6050 SDA PIN58 | MPU6050 INT PIN67
*    Left  motor PWM PIN25 (AF5 = PWM3) | Right motor PWM PIN20 (AF5 = PWM4)
*    Left  encoder A/B  PIN28 / PIN29
*    Right encoder A/B  PIN23 / PIN66
*
*    TB6612 direction  AIN1 PIN62 | AIN2 PIN49 | BIN1 PIN63 | BIN2 PIN64
*  PWM pinmux: Alternate Function 5 (PIN25 = PWM3 = sel 3, PIN20 = PWM4 = sel 4); sel n = PWMn
*  I2C: I2C0 + pinmux func 2 on PIN57/58, confirmed from small-screen-demo
**********************************************************************/
#include "qosa_def.h"
#include "qosa_sys.h"
#include "qosa_log.h"
#include "qosa_gpio.h"
#include "qosa_pwm.h"
#include "qosa_iic.h"
#include "qosa_pinctrl.h"
#include "qosa_dev_eigen.h"
#include "unirtos_app_init_registry.h"
#include <stdint.h>

#define QOS_LOG_TAG LOG_TAG_DEMO

/*===========================================================================
 *  User configuration
 ===========================================================================*/
/* ---- module pins ---- */
#define PIN_I2C_SCL   57
#define PIN_I2C_SDA   58
#define PIN_MPU_INT   67
#define PIN_PWM_L     25
#define PIN_PWM_R     20
#define PIN_ENC_L_A   28
#define PIN_ENC_L_B   29
#define PIN_ENC_R_A   23
#define PIN_ENC_R_B   66

/* TB6612 direction pins (module PIN numbers, GPIO) */
#define PIN_AIN1      62
#define PIN_AIN2      49
#define PIN_BIN1      63
#define PIN_BIN2      64

/* ---- PWM ---- */
#define PWM_SEL_L       3   /* PIN25 AF5 = PWM3 = sel 3 (independent channel from the right wheel) */
#define PWM_SEL_R       4   /* PIN20 AF5 = PWM4 = sel 4 (confirmed: duty changes speed) */
#define PWM_PIN_FUNC    5   /* PIN20 pin function that carries PWM4 */
#define PWM_PIN_FUNC_L  5   /* PIN25 pin function that carries PWM3 */
#define PWM_L_INVERT    0   /* PIN25 AF5 is the true (non-n) output, expected not inverted - verify in the self-test */
#define PWM_R_INVERT    0   /* right wheel (PIN20 func 5, sel 4): measured, speed rises with duty -> not inverted */
#define PWM_TOTAL       2600 /* 26 MHz / 2600 = 10 kHz (assuming pwm_psc = 1 means no division) */
#define PWM_PSC         1

/* ---- MPU6050 I2C ---- */
#define MPU_ADDR        0x68
#define MPU_I2C_CH      QOSA_I2C_0   /* PIN57/PIN58 = I2C0, taken from small-screen-demo (TP_IIC_PORT = 0) */
#define MPU_I2C_PIN_FUNC 2           /* pinmux func 2 = I2C on PIN57/58, taken from small-screen-demo */
#define MPU_USE_SOFT_I2C_FALLBACK 1  /* if hardware I2C cannot talk to the MPU6050, fall back to GPIO bit-bang */
#define SOFT_I2C_DELAY  8

/* ---- control (same as STM32 project, gains x100) ---- */
#define BALANCE_KP      25500.0f
#define BALANCE_KD      135.0f
#define VELOCITY_KP     16000.0f
#define VELOCITY_KI     80.0f
#define MIDDLE_ANGLE    8.6f      /* mechanical balance angle, tune to your car */
#define MOTOR_LIMIT     6900
#define MOTOR_FULL      7200      /* STM32 PWM period, control output scale */
#define ANGLE_CUTOFF    40.0f     /* motors off beyond +-40 deg */
#define ANGLE_START     3.0f      /* auto start when held within +-3 deg ... */
#define START_HOLD_CNT  200       /* ... for 200 cycles (~1 s) */

/* sign conventions: forward must give +encoder and +motor.  Flip if the car runs away the wrong way */
#define ENC_L_SIGN      (-1)
#define ENC_R_SIGN      (-1)
#define MOTOR_L_SIGN    (1)
#define MOTOR_R_SIGN    (1)

/* power-on motor self-test: spins each wheel ~0.6 s forward, then ~0.6 s backward (keep wheels off the ground!) */
#define MOTOR_SELFTEST  0
/* after the self-test: automatic motor-driven encoder test, one summary log line per step */
#define ENC_HAND_TEST   0

/* debug: at startup call qosa_pwm_config() for sel 0..7 and log which ones the SDK accepts (config only, output is not enabled) */
#define PWM_PROBE       0    /* 0 off, 1 old scan, 2 PIN19 func scan, 3 PIN19 func x duty sweep, 4 PIN19 duty staircase, 5 PIN20 func scan at fixed sel */
#define PWM_PROBE_FUNC  3    /* PWM_PROBE==4: PIN19 pin function under test */
#define PWM_PROBE_SEL   3    /* PWM_PROBE==2/3/4/5: the single sdk pwm sel (0..4) tested in this build */
#define IO_VOLTAGE_3V3  1    /* 1: power on AON GPIO and set GPIO voltage to 3.3V before any pin setup */
#define PWM_SCAN_ON_S   6    /* seconds each pwm channel runs */
#define PWM_SCAN_GAP_S  3    /* seconds of stop between channels */

#define CTRL_TASK_STACK 8192
#define CTRL_TASK_PRIO  QOSA_PRIORITY_HIGH

/*===========================================================================
 *  MPU6050 registers
 ===========================================================================*/
#define MPU_REG_SMPLRT_DIV   0x19
#define MPU_REG_CONFIG       0x1A
#define MPU_REG_GYRO_CFG     0x1B
#define MPU_REG_ACCEL_CFG    0x1C
#define MPU_REG_INT_PIN_CFG  0x37
#define MPU_REG_INT_ENABLE   0x38
#define MPU_REG_ACCEL_XOUT_H 0x3B
#define MPU_REG_PWR_MGMT_1   0x6B
#define MPU_REG_PWR_MGMT_2   0x6C
#define MPU_REG_WHO_AM_I     0x75

/*===========================================================================
 *  Variables
 ===========================================================================*/
static qosa_task_t g_car_task = QOSA_NULL;
static qosa_sem_t  g_int_sem  = QOSA_NULL;

static int g_gpio_int = -1;
static int g_gpio_scl = -1, g_gpio_sda = -1;
static int g_gpio_dir[4] = {-1, -1, -1, -1}; /* AIN1 AIN2 BIN1 BIN2 */
static int g_gpio_enc[4] = {-1, -1, -1, -1}; /* LA LB RA RB */
static int g_i2c_soft    = 0;
static int g_pwm_on[2]   = {0, 0};

/* encoder: each counter is written by exactly one ISR, the task only reads and differences them */
static volatile int32_t g_cnt_la, g_cnt_lb, g_cnt_ra, g_cnt_rb;
static volatile uint32_t g_int_cnt;

/*===========================================================================
 *  Small math helpers (no libm dependency)
 ===========================================================================*/
static float f_abs(float v)
{
    return v < 0 ? -v : v;
}

static int i_abs(int v)
{
    return v < 0 ? -v : v;
}

static float atan2_deg(float y, float x)
{
    float ax = f_abs(x), ay = f_abs(y), a, s, r;
    if (ax == 0.0f && ay == 0.0f)
        return 0.0f;
    a = (ax >= ay) ? (ay / ax) : (ax / ay);
    s = a * a;
    r = ((((0.0208351f * s - 0.0851330f) * s + 0.1801410f) * s - 0.3302995f) * s + 0.9998660f) * a;
    if (ay > ax)
        r = 1.5707963f - r;
    if (x < 0)
        r = 3.1415927f - r;
    if (y < 0)
        r = -r;
    return r * 57.29578f;
}

/* Kalman filter, identical to Kalman_Filter_x() of the STM32 project, dt passed in */
static float kalman_x(float accel_angle, float gyro_dps, float dt)
{
    static float angle = 0.0f, q_bias = 0.0f;
    static float pp[2][2] = {{1, 0}, {0, 1}};
    const float  q_angle = 0.001f, q_gyro = 0.003f, r_angle = 0.5f;
    float        pdot[4], angle_err, pct_0, pct_1, e, k_0, k_1, t_0, t_1;

    angle += (gyro_dps - q_bias) * dt;
    pdot[0] = q_angle - pp[0][1] - pp[1][0];
    pdot[1] = -pp[1][1];
    pdot[2] = -pp[1][1];
    pdot[3] = q_gyro;
    pp[0][0] += pdot[0] * dt;
    pp[0][1] += pdot[1] * dt;
    pp[1][0] += pdot[2] * dt;
    pp[1][1] += pdot[3] * dt;

    angle_err = accel_angle - angle;
    pct_0     = pp[0][0];
    pct_1     = pp[1][0];
    e         = r_angle + pct_0;
    k_0       = pct_0 / e;
    k_1       = pct_1 / e;
    t_0       = pct_0;
    t_1       = pp[0][1];
    pp[0][0] -= k_0 * t_0;
    pp[0][1] -= k_0 * t_1;
    pp[1][0] -= k_1 * t_0;
    pp[1][1] -= k_1 * t_1;
    angle += k_0 * angle_err;
    q_bias += k_1 * angle_err;
    return angle;
}

/*===========================================================================
 *  GPIO helpers
 ===========================================================================*/
/* switch module pin to its GPIO function and return GPIO number, -1 if pin invalid / unused */
static int pin_to_gpio(int pin)
{
    qosa_pin_cfg_t cfg;
    if (pin <= 0)
        return -1;
    if (qosa_get_pin_default_cfg((qosa_uint8_t)pin, &cfg) != QOSA_GPIO_SUCCESS)
    {
        QLOGE("[balancing_car] pin %d: get default cfg failed", pin);
        return -1;
    }
    qosa_pin_set_func((qosa_pin_num_e)pin, cfg.gpio_func);
    return (int)cfg.gpio_num;
}

static void gpio_out(int gpio, int level)
{
    if (gpio >= 0)
        qosa_gpio_set_level((qosa_gpio_num_e)gpio, level ? QOSA_GPIO_LEVEL_HIGH : QOSA_GPIO_LEVEL_LOW);
}

static int gpio_in(int gpio)
{
    qosa_gpio_level_e lv = QOSA_GPIO_LEVEL_LOW;
    qosa_gpio_get_level((qosa_gpio_num_e)gpio, &lv);
    return lv == QOSA_GPIO_LEVEL_HIGH;
}

/*===========================================================================
 *  Encoders (GPIO both-edge interrupts, X4 decoding)
 *    A edge : up if A != B      B edge : up if A == B     (A leads B = positive)
 ===========================================================================*/
#define ENC_ISR(name, cnt, ga, gb, a_edge)                                  \
    static void name(void *ctx)                                             \
    {                                                                       \
        int a = gpio_in(g_gpio_enc[ga]);                                    \
        int b = gpio_in(g_gpio_enc[gb]);                                    \
        int up = (a_edge) ? (a != b) : (a == b);                            \
        cnt += up ? 1 : -1;                                                 \
    }

ENC_ISR(enc_isr_la, g_cnt_la, 0, 1, 1)
ENC_ISR(enc_isr_lb, g_cnt_lb, 0, 1, 0)
ENC_ISR(enc_isr_ra, g_cnt_ra, 2, 3, 1)
ENC_ISR(enc_isr_rb, g_cnt_rb, 2, 3, 0)

static void enc_attach(int idx, int pin, gpio_interrupt_cb cb)
{
    qosa_int_cfg_t cfg;
    qosa_gpio_error_e r;
    g_gpio_enc[idx] = pin_to_gpio(pin);
    if (g_gpio_enc[idx] < 0)
    {
        QLOGE("[balancing_car] enc%d PIN%d: no gpio", idx, pin);
        return;
    }
    cfg.gpio_num      = (qosa_gpio_num_e)g_gpio_enc[idx];
    cfg.gpio_debounce = QOSA_GPIO_DEBOUNCE_DIS;
    cfg.gpio_pull     = QOSA_GPIO_PULL_UP;
    cfg.interrupt_cb  = cb;
    cfg.options       = 1; /* multiple-interrupt mode */
    cfg.user_ctx      = QOSA_NULL;
    r = qosa_interrupt_register(&cfg);
    QLOGI("[balancing_car] enc%d PIN%d -> GPIO%d register ret=0x%x", idx, pin, g_gpio_enc[idx], (unsigned)r);
}

static void encoders_init(void)
{
    enc_attach(0, PIN_ENC_L_A, enc_isr_la);
    enc_attach(1, PIN_ENC_L_B, enc_isr_lb);
    enc_attach(2, PIN_ENC_R_A, enc_isr_ra);
    enc_attach(3, PIN_ENC_R_B, enc_isr_rb);
    /* enable only after all four GPIO numbers are known (ISRs read the partner phase) */
    for (int i = 0; i < 4; i++)
        if (g_gpio_enc[i] >= 0)
        {
            qosa_gpio_error_e r = qosa_interrupt_enable((qosa_gpio_num_e)g_gpio_enc[i], QOSA_GPIO_TRIGGER_BOTH_EDGE);
            QLOGI("[balancing_car] enc%d GPIO%d enable ret=0x%x level=%d", i, g_gpio_enc[i], (unsigned)r, gpio_in(g_gpio_enc[i]));
        }
}

/*===========================================================================
 *  I2C (hardware, with GPIO bit-bang fallback)
 ===========================================================================*/
#define SOFT_DLY()                                           \
    do                                                       \
    {                                                        \
        volatile int _d;                                     \
        for (_d = 0; _d < SOFT_I2C_DELAY; _d++)              \
            ;                                                \
    } while (0)

static void scl_h(void) { gpio_out(g_gpio_scl, 1); }
static void scl_l(void) { gpio_out(g_gpio_scl, 0); }
static void sda_h(void) { qosa_gpio_set_direction((qosa_gpio_num_e)g_gpio_sda, QOSA_GPIO_DIRECTION_INPUT); }
static void sda_l(void)
{
    qosa_gpio_set_level((qosa_gpio_num_e)g_gpio_sda, QOSA_GPIO_LEVEL_LOW);
    qosa_gpio_set_direction((qosa_gpio_num_e)g_gpio_sda, QOSA_GPIO_DIRECTION_OUTPUT);
}

static void soft_start(void)
{
    sda_h(); scl_h(); SOFT_DLY();
    sda_l(); SOFT_DLY();
    scl_l();
}

static void soft_stop(void)
{
    sda_l(); SOFT_DLY();
    scl_h(); SOFT_DLY();
    sda_h(); SOFT_DLY();
}

static int soft_wbyte(uint8_t b)
{
    int ack;
    for (int i = 0; i < 8; i++)
    {
        if (b & 0x80) sda_h(); else sda_l();
        SOFT_DLY();
        scl_h(); SOFT_DLY();
        scl_l();
        b <<= 1;
    }
    sda_h(); SOFT_DLY();
    scl_h(); SOFT_DLY();
    ack = !gpio_in(g_gpio_sda);
    scl_l();
    return ack;
}

static uint8_t soft_rbyte(int ack)
{
    uint8_t v = 0;
    sda_h();
    for (int i = 0; i < 8; i++)
    {
        SOFT_DLY();
        scl_h(); SOFT_DLY();
        v = (uint8_t)((v << 1) | gpio_in(g_gpio_sda));
        scl_l();
    }
    if (ack) sda_l(); else sda_h();
    SOFT_DLY();
    scl_h(); SOFT_DLY();
    scl_l();
    sda_h();
    return v;
}

static int soft_i2c_init(void)
{
    g_gpio_scl = pin_to_gpio(PIN_I2C_SCL);
    g_gpio_sda = pin_to_gpio(PIN_I2C_SDA);
    if (g_gpio_scl < 0 || g_gpio_sda < 0)
        return -1;
    qosa_gpio_init((qosa_gpio_num_e)g_gpio_scl, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH);
    qosa_gpio_init((qosa_gpio_num_e)g_gpio_sda, QOSA_GPIO_DIRECTION_INPUT, QOSA_GPIO_PULL_UP, QOSA_GPIO_LEVEL_HIGH);
    return 0;
}

static int mpu_read(uint8_t reg, uint8_t *buf, uint32_t len)
{
    if (!g_i2c_soft)
        return qosa_i2c_read(MPU_I2C_CH, MPU_ADDR, reg, buf, len) == QOSA_I2C_SUCCESS ? 0 : -1;

    soft_start();
    if (!soft_wbyte((MPU_ADDR << 1) | 0)) { soft_stop(); return -1; }
    if (!soft_wbyte(reg))                 { soft_stop(); return -1; }
    soft_start();
    if (!soft_wbyte((MPU_ADDR << 1) | 1)) { soft_stop(); return -1; }
    for (uint32_t i = 0; i < len; i++)
        buf[i] = soft_rbyte(i + 1 < len);
    soft_stop();
    return 0;
}

static int mpu_write(uint8_t reg, uint8_t val)
{
    if (!g_i2c_soft)
        return qosa_i2c_write(MPU_I2C_CH, MPU_ADDR, reg, &val, 1) == QOSA_I2C_SUCCESS ? 0 : -1;

    soft_start();
    if (!soft_wbyte((MPU_ADDR << 1) | 0)) { soft_stop(); return -1; }
    if (!soft_wbyte(reg))                 { soft_stop(); return -1; }
    if (!soft_wbyte(val))                 { soft_stop(); return -1; }
    soft_stop();
    return 0;
}

static int mpu_probe(void)
{
    uint8_t id = 0;
    if (mpu_read(MPU_REG_WHO_AM_I, &id, 1) != 0)
        return -1;
    QLOGI("[balancing_car] MPU WHO_AM_I=0x%x (%s I2C)", id, g_i2c_soft ? "soft" : "hw");
    return (id == 0x00 || id == 0xFF) ? -1 : 0;
}

/*===========================================================================
 *  MPU6050
 ===========================================================================*/
static void INT_isr(void *ctx)
{
    g_int_cnt++;
    qosa_sem_release(g_int_sem);
}

static int mpu_init(void)
{
    /* 1) try hardware I2C */
    g_i2c_soft = 0;
    if (MPU_I2C_PIN_FUNC >= 0)
    {
        qosa_pin_set_func((qosa_pin_num_e)PIN_I2C_SCL, MPU_I2C_PIN_FUNC);
        qosa_pin_set_func((qosa_pin_num_e)PIN_I2C_SDA, MPU_I2C_PIN_FUNC);
    }
    if (qosa_i2c_init(MPU_I2C_CH, QOSA_IIC_FAST_MODE) != QOSA_I2C_SUCCESS || mpu_probe() != 0)
    {
        QLOGW("[balancing_car] hardware I2C failed");
#if MPU_USE_SOFT_I2C_FALLBACK
        qosa_i2c_deinit(MPU_I2C_CH);
        if (soft_i2c_init() != 0)
            return -1;
        g_i2c_soft = 1;
        if (mpu_probe() != 0)
        {
            QLOGE("[balancing_car] MPU6050 not found on PIN%d/PIN%d", PIN_I2C_SCL, PIN_I2C_SDA);
            return -1;
        }
#else
        return -1;
#endif
    }

    mpu_write(MPU_REG_PWR_MGMT_1, 0x80); /* reset */
    qosa_task_sleep_ms(100);
    mpu_write(MPU_REG_PWR_MGMT_1, 0x01); /* wake, clock = PLL gyro X */
    qosa_task_sleep_ms(10);
    mpu_write(MPU_REG_PWR_MGMT_2, 0x00);
    mpu_write(MPU_REG_GYRO_CFG, 0x18);   /* +-2000 dps  (16.4 LSB/dps) */
    mpu_write(MPU_REG_ACCEL_CFG, 0x00);  /* +-2 g       (16384 LSB/g)  */
    mpu_write(MPU_REG_CONFIG, 0x03);     /* DLPF 42 Hz, gyro rate 1 kHz */
    mpu_write(MPU_REG_SMPLRT_DIV, 4);    /* 1 kHz / 5 = 200 Hz -> 5 ms  */
    mpu_write(MPU_REG_INT_PIN_CFG, 0x30);/* active high, push-pull, latched, cleared on any read */
    mpu_write(MPU_REG_INT_ENABLE, 0x01); /* data ready interrupt */
    qosa_task_sleep_ms(50);
    return 0;
}

static int mpu_read_raw(int16_t *ax, int16_t *ay, int16_t *az, int16_t *gx, int16_t *gz)
{
    uint8_t b[14];
    if (mpu_read(MPU_REG_ACCEL_XOUT_H, b, 14) != 0)
        return -1;
    *ax = (int16_t)((b[0] << 8) | b[1]);
    *ay = (int16_t)((b[2] << 8) | b[3]);
    *az = (int16_t)((b[4] << 8) | b[5]);
    *gx = (int16_t)((b[8] << 8) | b[9]);
    *gz = (int16_t)((b[12] << 8) | b[13]);
    return 0;
}

static void mpu_int_init(void)
{
    qosa_int_cfg_t cfg;
    qosa_sem_create_ex(&g_int_sem, 0, 1);
    g_gpio_int = pin_to_gpio(PIN_MPU_INT);
    QLOGI("[balancing_car] MPU INT: PIN%d -> GPIO%d (expect GPIO9)", PIN_MPU_INT, g_gpio_int);
    if (g_gpio_int < 0)
        return;
    cfg.gpio_num      = (qosa_gpio_num_e)g_gpio_int;
    cfg.gpio_debounce = QOSA_GPIO_DEBOUNCE_DIS;
    cfg.gpio_pull     = QOSA_GPIO_PULL_DOWN;
    cfg.interrupt_cb  = INT_isr;
    cfg.options       = 1;
    cfg.user_ctx      = QOSA_NULL;
    qosa_interrupt_register(&cfg);
    qosa_interrupt_enable((qosa_gpio_num_e)g_gpio_int, QOSA_GPIO_TRIGGER_RISING_EDGE);
}

/*===========================================================================
 *  Motors
 ===========================================================================*/
static void pwm_set(int idx, int duty)
{
    unsigned char sel = idx ? PWM_SEL_R : PWM_SEL_L;
    qosa_pwm_info_t info;
    qosa_pwm_error_e ret = QOSA_PWM_SUCESS;
    static int logged[2] = {0, 0};

    if (duty <= 0)
        duty = 0;   /* never disable: right wheel PWM (sel 3) only works while the left channel (sel 4) stays up; AIN/BIN low = stop */
    if (duty >= PWM_TOTAL)
        duty = PWM_TOTAL - 1;
    if (idx ? PWM_R_INVERT : PWM_L_INVERT)
        duty = PWM_TOTAL - duty;
    if (duty < 1)
        duty = 1;
    if (duty >= PWM_TOTAL)
        duty = PWM_TOTAL - 1;

    if (!g_pwm_on[idx])
    {
        info.clk_src                  = QOSA_FCLK_SEL_26M;
        info.total_one_cycle_duration = PWM_TOTAL;
        info.high_one_cycle_duration  = duty;
        info.pwm_psc                  = PWM_PSC;
        ret = qosa_pwm_config(sel, &info);
        if (!logged[idx])
            QLOGI("[balancing_car] pwm%d config ret=0x%x", sel, (unsigned)ret);
        if (ret == QOSA_PWM_SUCESS)
        {
            /* pwm config may reset the pad mux: re-apply the pin functions of both wheels after it */
            qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PIN_FUNC_L);
            qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_R, PWM_PIN_FUNC);
            g_pwm_on[idx] = 1;
        }
        else if (!logged[idx])
            QLOGE("[balancing_car] pwm%d config failed, wheel %d has no PWM output", sel, idx);
    }
    if (g_pwm_on[idx])
    {
        ret = qosa_pwm_enable(sel, duty);
        if (!logged[idx])
            QLOGI("[balancing_car] pwm%d enable duty=%d ret=0x%x", sel, duty, (unsigned)ret);
    }
    logged[idx] = 1;
}

static void motor_set_one(int idx, int value)
{
    int in1 = g_gpio_dir[idx * 2], in2 = g_gpio_dir[idx * 2 + 1];
    if (value > 0) { gpio_out(in1, 1); gpio_out(in2, 0); }
    else           { gpio_out(in1, 0); gpio_out(in2, 1); }
    pwm_set(idx, i_abs(value) * PWM_TOTAL / MOTOR_FULL);
}

static void motor_set(int left, int right)
{
    motor_set_one(0, left * MOTOR_L_SIGN);
    motor_set_one(1, right * MOTOR_R_SIGN);
}

static void motor_stop(void)
{
    pwm_set(0, 0);
    pwm_set(1, 0);
    for (int i = 0; i < 4; i++)
        gpio_out(g_gpio_dir[i], 0);
}

static void motor_init(void)
{
    const int dir_pins[4] = {PIN_AIN1, PIN_AIN2, PIN_BIN1, PIN_BIN2};
    qosa_pin_cfg_t cfg;

    for (int i = 0; i < 4; i++)
    {
        g_gpio_dir[i] = pin_to_gpio(dir_pins[i]);
        if (g_gpio_dir[i] >= 0)
            qosa_gpio_init((qosa_gpio_num_e)g_gpio_dir[i], QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_LOW);
    }
    QLOGI("[balancing_car] dir gpio: AIN1=%d AIN2=%d BIN1=%d BIN2=%d", g_gpio_dir[0], g_gpio_dir[1], g_gpio_dir[2], g_gpio_dir[3]);
    if (g_gpio_dir[0] < 0 || g_gpio_dir[2] < 0)
        QLOGW("[balancing_car] motor direction pins not configured (PIN_AIN1/AIN2/BIN1/BIN2): car cannot reverse wheels");

    if (qosa_get_pin_default_cfg(PIN_PWM_L, &cfg) == QOSA_GPIO_SUCCESS)
        QLOGI("[balancing_car] PIN%d default func=%d gpio func=%d", PIN_PWM_L, cfg.default_func, cfg.gpio_func);
    if (qosa_get_pin_default_cfg(PIN_PWM_R, &cfg) == QOSA_GPIO_SUCCESS)
        QLOGI("[balancing_car] PIN%d default func=%d gpio func=%d", PIN_PWM_R, cfg.default_func, cfg.gpio_func);
    {
        /* same as the working scan: bring the pads up as GPIO outputs first, then switch to AF5 */
        int gl = pin_to_gpio(PIN_PWM_L), gr = pin_to_gpio(PIN_PWM_R);
        if (gl >= 0) qosa_gpio_init((qosa_gpio_num_e)gl, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_LOW);
        if (gr >= 0) qosa_gpio_init((qosa_gpio_num_e)gr, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_LOW);
    }
    qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PIN_FUNC_L);
    qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_R, PWM_PIN_FUNC);
    /* bring both channels up in this order (left sel 4 first, then right sel 3) and leave them running; direction pins are low */
    pwm_set(0, 0);
    pwm_set(1, 0);
}

/*===========================================================================
 *  Controllers (same formulas as control.c)
 ===========================================================================*/
static int balance_ctrl(float angle, float gyro)
{
    float angle_bias = MIDDLE_ANGLE - angle;
    float gyro_bias  = 0 - gyro;
    return (int)(-BALANCE_KP / 100 * angle_bias - gyro_bias * BALANCE_KD / 100);
}

static int velocity_ctrl(int enc_l, int enc_r, int reset)
{
    static float enc_bias = 0, enc_integral = 0;
    float        enc_least = 0 - (float)(enc_l + enc_r);

    enc_bias *= 0.86f;
    enc_bias += enc_least * 0.14f;
    enc_integral += enc_bias;
    if (enc_integral > 10000)  enc_integral = 10000;
    if (enc_integral < -10000) enc_integral = -10000;
    if (reset)
        enc_integral = 0;
    return (int)(-enc_bias * VELOCITY_KP / 100 - enc_integral * VELOCITY_KI / 100);
}

static int limit(int v, int lo, int hi)
{
    return v > hi ? hi : (v < lo ? lo : v);
}

#if PWM_PROBE == 2 || PWM_PROBE == 3
/* left wheel full-speed pulse: PIN19 driven as GPIO high, AIN1/AIN2 set for forward / reverse */
static void left_gpio_pulse(int forward, int ms)
{
    int gl = pin_to_gpio(PIN_PWM_L);
    if (gl >= 0)
        qosa_gpio_init((qosa_gpio_num_e)gl, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_HIGH);
    gpio_out(gl, 1);
    gpio_out(g_gpio_dir[0], forward ? 1 : 0);
    gpio_out(g_gpio_dir[1], forward ? 0 : 1);
    qosa_task_sleep_ms(ms);
    gpio_out(g_gpio_dir[0], 0);
    gpio_out(g_gpio_dir[1], 0);
    gpio_out(gl, 0);
}
#endif

#if PWM_PROBE == 5
/* right wheel full-speed pulse: PIN20 driven as GPIO high, BIN1/BIN2 set for forward / reverse */
static void right_gpio_pulse(int forward, int ms)
{
    int gr = pin_to_gpio(PIN_PWM_R);
    if (gr >= 0)
        qosa_gpio_init((qosa_gpio_num_e)gr, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_HIGH);
    gpio_out(gr, 1);
    gpio_out(g_gpio_dir[2], forward ? 1 : 0);
    gpio_out(g_gpio_dir[3], forward ? 0 : 1);
    qosa_task_sleep_ms(ms);
    gpio_out(g_gpio_dir[2], 0);
    gpio_out(g_gpio_dir[3], 0);
    gpio_out(gr, 0);
}
#endif

/*===========================================================================
 *  Control task
 ===========================================================================*/
static void balancing_car_task(void *ctx)
{
    int16_t  ax, ay, az, gx, gz;
    int32_t  last_la = 0, last_lb = 0, last_ra = 0, last_rb = 0;
    int      stopped = 1, hold_cnt = 0, use_int = 1, fail_cnt = 0, log_cnt = 0;
    uint32_t last_int = 0;
    int      no_int_cycles = 0;

    qosa_task_sleep_ms(2000); /* let the system finish booting */
    QLOGI("[balancing_car] balancing car start");

#if IO_VOLTAGE_3V3
    {
        qosa_gpio_error_e e1 = qosa_aon_gpio_power_control(POWER_ON);
        qosa_gpio_error_e e2 = qosa_gpio_set_voltage(VOL_3_30V);
        QLOGI("[balancing_car] aon gpio power on ret=0x%x, set voltage 3.3V ret=0x%x", (unsigned)e1, (unsigned)e2);
    }
#endif
    motor_init();
    mpu_int_init();
    encoders_init();
    motor_stop();
#if PWM_PROBE == 2
    /* PIN19 scan: left wheel only (right wheel direction pins stay low), forward, 50 % PWM.
       One build tests one sel (PWM_PROBE_SEL); loop over PIN19 pin function f = 1..7. */
    {
        qosa_pwm_info_t pi;
        pi.clk_src                  = QOSA_FCLK_SEL_26M;
        pi.total_one_cycle_duration = PWM_TOTAL;
        pi.high_one_cycle_duration  = PWM_TOTAL / 2;
        pi.pwm_psc                  = PWM_PSC;
        QLOGI("[balancing_car] PIN19 SCAN start, sel=%d, func 1..7", PWM_PROBE_SEL);
        /* Only sel = PWM_PROBE_SEL is tested; PIN19 function f = 1..7, one slot each.
           Marker before slot f: f REVERSE pulses of the left wheel (0.3 s on / 0.3 s off), then 1.5 s pause.
           Slot: 2.5 s PWM (50 %) forward, then 1.5 s stop.
           Note the f (number of reverse pulses) after which the left wheel turns for ~2.5 s. */
        qosa_task_sleep_ms(3000);
        for (int f = 1; f <= 7; f++)
        {
            const int s = PWM_PROBE_SEL;
            for (int k = 0; k < f; k++)
            {
                left_gpio_pulse(0, 300);
                qosa_task_sleep_ms(300);
            }
            qosa_task_sleep_ms(1500);
            {
                qosa_pwm_error_e pr, pe;
                int gl;
                gl = pin_to_gpio(PIN_PWM_L);
                if (gl >= 0) qosa_gpio_init((qosa_gpio_num_e)gl, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_LOW);
                qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, f);
                pr = qosa_pwm_config((unsigned char)s, &pi);
                qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, f);
                pe = qosa_pwm_enable((unsigned char)s, PWM_TOTAL / 2);
                QLOGI("[balancing_car] func=%d sel=%d config=0x%x enable=0x%x", f, s, (unsigned)pr, (unsigned)pe);
                gpio_out(g_gpio_dir[0], 1); gpio_out(g_gpio_dir[1], 0);
                qosa_task_sleep_ms(2500);
                qosa_pwm_disable((unsigned char)s);
                gpio_out(g_gpio_dir[0], 0); gpio_out(g_gpio_dir[1], 0);
                qosa_task_sleep_ms(1500);
            }
        }
        QLOGI("[balancing_car] PIN19 SCAN done");
        qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PIN_FUNC);
    }
#endif
#if PWM_PROBE == 3
    /* PIN19 func x duty sweep at fixed sel = PWM_PROBE_SEL, PIN19 pin function f = 1..7.
       Before func f: f REVERSE pulses of the left wheel, 1.5 s pause.
       Then forward 20 % PWM for 2.5 s, stop 1 s, forward 80 % PWM for 2.5 s, stop 1.5 s.
       A real PWM output: the 80 % run is clearly faster than the 20 % run. */
    {
        const int s = PWM_PROBE_SEL;
        qosa_pwm_info_t pi;
        pi.clk_src                  = QOSA_FCLK_SEL_26M;
        pi.total_one_cycle_duration = PWM_TOTAL;
        pi.high_one_cycle_duration  = PWM_TOTAL / 2;
        pi.pwm_psc                  = PWM_PSC;
        QLOGI("[balancing_car] PIN19 DUTY SWEEP start, sel=%d, func 1..7", s);
        qosa_task_sleep_ms(3000);
        for (int f = 1; f <= 7; f++)
        {
            static const int duty_pct[2] = { 20, 80 };
            for (int k = 0; k < f; k++)
            {
                left_gpio_pulse(0, 300);
                qosa_task_sleep_ms(300);
            }
            qosa_task_sleep_ms(1500);
            for (int d = 0; d < 2; d++)
            {
                qosa_pwm_error_e pr, pe;
                int gl = pin_to_gpio(PIN_PWM_L);
                int hi = PWM_TOTAL * duty_pct[d] / 100;
                if (gl >= 0) qosa_gpio_init((qosa_gpio_num_e)gl, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_LOW);
                qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, f);
                pi.high_one_cycle_duration = hi;
                pr = qosa_pwm_config((unsigned char)s, &pi);
                qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, f);
                pe = qosa_pwm_enable((unsigned char)s, hi);
                QLOGI("[balancing_car] func=%d sel=%d duty=%d%% config=0x%x enable=0x%x", f, s, duty_pct[d], (unsigned)pr, (unsigned)pe);
                gpio_out(g_gpio_dir[0], 1); gpio_out(g_gpio_dir[1], 0);
                qosa_task_sleep_ms(2500);
                qosa_pwm_disable((unsigned char)s);
                gpio_out(g_gpio_dir[0], 0); gpio_out(g_gpio_dir[1], 0);
                qosa_task_sleep_ms(d == 0 ? 1000 : 1500);
            }
        }
        QLOGI("[balancing_car] PIN19 DUTY SWEEP done");
        qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PIN_FUNC);
    }
#endif
#if PWM_PROBE == 4
    /* PIN19 duty staircase: one config only, PWM stays enabled the whole time, only AIN1/AIN2 gate the motion
       (both low = stop). Pass 1 duty 10,30,50,70,90 %, pass 2 duty 90,70,50,30,10 %. Each run 2.5 s, stop 1 s.
       Real PWM: speed rises with duty in pass 1 and falls in pass 2. */
    {
        static const int duty_pct[10] = { 10, 30, 50, 70, 90, 90, 70, 50, 30, 10 };
        const int s = PWM_PROBE_SEL;
        qosa_pwm_info_t pi;
        qosa_pwm_error_e pr, pe;
        int gl = pin_to_gpio(PIN_PWM_L);
        pi.clk_src                  = QOSA_FCLK_SEL_26M;
        pi.total_one_cycle_duration = PWM_TOTAL;
        pi.high_one_cycle_duration  = PWM_TOTAL / 10;
        pi.pwm_psc                  = PWM_PSC;
        QLOGI("[balancing_car] PIN19 STAIRCASE start, func=%d sel=%d", PWM_PROBE_FUNC, s);
        qosa_task_sleep_ms(3000);
        if (gl >= 0) qosa_gpio_init((qosa_gpio_num_e)gl, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_LOW);
        qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PROBE_FUNC);
        pr = qosa_pwm_config((unsigned char)s, &pi);
        qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PROBE_FUNC);
        QLOGI("[balancing_car] config ret=0x%x", (unsigned)pr);
        for (int i = 0; i < 10; i++)
        {
            int hi = PWM_TOTAL * duty_pct[i] / 100;
            pe = qosa_pwm_enable((unsigned char)s, hi);
            QLOGI("[balancing_car] step %d: duty=%d%% enable ret=0x%x", i + 1, duty_pct[i], (unsigned)pe);
            gpio_out(g_gpio_dir[0], 1); gpio_out(g_gpio_dir[1], 0);
            qosa_task_sleep_ms(2500);
            gpio_out(g_gpio_dir[0], 0); gpio_out(g_gpio_dir[1], 0);
            qosa_task_sleep_ms(1000);
        }
        qosa_pwm_disable((unsigned char)s);
        QLOGI("[balancing_car] PIN19 STAIRCASE done");
        qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PIN_FUNC);
    }
#endif
#if PWM_PROBE == 5
    /* PIN20 scan, 4 slots (left channel sel 4 is brought up by motor_init first):
       slot 1..4 = (sel4,f3) (sel4,f5) (sel3,f3) (sel3,f5)  - sel 0..2 already tested: no effect.
       Before slot n: n REVERSE pulses of the RIGHT wheel (0.3 s on / 0.3 s off), 1.5 s pause.
       Then forward 10 % PWM for 2 s, immediately 90 % PWM for 2 s, stop 1.5 s.
       Real PWM: the 90 % part is clearly faster (or slower if inverted) than the 10 % part. */
    {
        static const int sels[4]  = { 4, 4, 3, 3 };
        static const int funcs[4] = { 3, 5, 3, 5 };
        qosa_pwm_info_t pi;
        pi.clk_src                  = QOSA_FCLK_SEL_26M;
        pi.total_one_cycle_duration = PWM_TOTAL;
        pi.pwm_psc                  = PWM_PSC;
        QLOGI("[balancing_car] PIN20 SCAN start, 4 slots");
        qosa_task_sleep_ms(3000);
        for (int slot = 0; slot < 4; slot++)
        {
            const int s = sels[slot];
            const int f = funcs[slot];
            qosa_pwm_error_e pr, pe1, pe2;
            int gr;
            for (int k = 0; k <= slot; k++)
            {
                right_gpio_pulse(0, 300);
                qosa_task_sleep_ms(300);
            }
            qosa_task_sleep_ms(1500);
            gr = pin_to_gpio(PIN_PWM_R);
            if (gr >= 0) qosa_gpio_init((qosa_gpio_num_e)gr, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_LOW);
            qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_R, f);
            pi.high_one_cycle_duration = PWM_TOTAL / 10;
            pr = qosa_pwm_config((unsigned char)s, &pi);
            qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_R, f);
            pe1 = qosa_pwm_enable((unsigned char)s, PWM_TOTAL / 10);
            gpio_out(g_gpio_dir[2], 1); gpio_out(g_gpio_dir[3], 0);
            qosa_task_sleep_ms(2000);
            pe2 = qosa_pwm_enable((unsigned char)s, PWM_TOTAL * 9 / 10);
            qosa_task_sleep_ms(2000);
            gpio_out(g_gpio_dir[2], 0); gpio_out(g_gpio_dir[3], 0);
            qosa_pwm_disable((unsigned char)s);
            QLOGI("[balancing_car] slot=%d func=%d sel=%d config=0x%x enable10=0x%x enable90=0x%x", slot + 1, f, s, (unsigned)pr, (unsigned)pe1, (unsigned)pe2);
            qosa_task_sleep_ms(1500);
        }
        QLOGI("[balancing_car] PIN20 SCAN done");
        /* restore normal state: bring both channels up again (left sel 4, then right sel 3) */
        qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PIN_FUNC_L);
        qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_R, PWM_PIN_FUNC);
        g_pwm_on[0] = 0;
        g_pwm_on[1] = 0;
        pwm_set(0, 0);
        pwm_set(1, 0);
    }
#endif
#if PWM_PROBE == 1
    /* PWM channel scan (IO is 3.3V now): for each sel 0..4 output 50% PWM for PWM_SCAN_ON_S seconds with both wheels
       set forward, then PWM_SCAN_GAP_S seconds of stop. Watch which wheel turns in each slot (order: sel0..sel4). */
    {
        qosa_task_sleep_ms(5000); /* time to get ready */

        /* Phase 1: no PWM. PIN19/PIN20 driven as plain GPIO HIGH (3.3V) = 100% duty. Wheels must spin at full speed
           (forward 8 s, stop 3 s, reverse 8 s). If they do not, the problem is hardware (STBY / VM / GND / wiring). */
        {
            int gl = pin_to_gpio(PIN_PWM_L), gr = pin_to_gpio(PIN_PWM_R);
            QLOGI("[balancing_car] PHASE 1 static: PWM pins gpio %d/%d HIGH, forward 8 s", gl, gr);
            if (gl >= 0) qosa_gpio_init((qosa_gpio_num_e)gl, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_HIGH);
            if (gr >= 0) qosa_gpio_init((qosa_gpio_num_e)gr, QOSA_GPIO_DIRECTION_OUTPUT, QOSA_GPIO_PULL_DOWN, QOSA_GPIO_LEVEL_HIGH);
            gpio_out(gl, 1); gpio_out(gr, 1);
            gpio_out(g_gpio_dir[0], 1); gpio_out(g_gpio_dir[1], 0);
            gpio_out(g_gpio_dir[2], 1); gpio_out(g_gpio_dir[3], 0);
            qosa_task_sleep_ms(8000);
            gpio_out(g_gpio_dir[0], 0); gpio_out(g_gpio_dir[1], 0);
            gpio_out(g_gpio_dir[2], 0); gpio_out(g_gpio_dir[3], 0);
            qosa_task_sleep_ms(3000);
            QLOGI("[balancing_car] PHASE 1 static: reverse 8 s");
            gpio_out(g_gpio_dir[0], 0); gpio_out(g_gpio_dir[1], 1);
            gpio_out(g_gpio_dir[2], 0); gpio_out(g_gpio_dir[3], 1);
            qosa_task_sleep_ms(8000);
            gpio_out(gl, 0); gpio_out(gr, 0);
            motor_stop();
            QLOGI("[balancing_car] PHASE 1 done");
            qosa_task_sleep_ms(3000);
        }

        /* Phase 2: PWM channel scan */
        for (int s = 0; s < 5; s++)
        {
            qosa_pwm_info_t pi;
            qosa_pwm_error_e pr, pe;
            pi.clk_src                  = QOSA_FCLK_SEL_26M;
            pi.total_one_cycle_duration = PWM_TOTAL;
            pi.high_one_cycle_duration  = PWM_TOTAL / 2;
            pi.pwm_psc                  = PWM_PSC;
            pr = qosa_pwm_config((unsigned char)s, &pi);
            qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PIN_FUNC);
            qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_R, PWM_PIN_FUNC);
            pe = qosa_pwm_enable((unsigned char)s, PWM_TOTAL / 2);
            QLOGI("[balancing_car] PWM SCAN sel=%d config=0x%x enable=0x%x, %d s", s, (unsigned)pr, (unsigned)pe, PWM_SCAN_ON_S);
            gpio_out(g_gpio_dir[0], 1); gpio_out(g_gpio_dir[1], 0);
            gpio_out(g_gpio_dir[2], 1); gpio_out(g_gpio_dir[3], 0);
            qosa_task_sleep_ms(PWM_SCAN_ON_S * 1000);
            qosa_pwm_disable((unsigned char)s);
            motor_stop();
            qosa_task_sleep_ms(PWM_SCAN_GAP_S * 1000);
        }
        QLOGI("[balancing_car] PWM SCAN done");
        qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_L, PWM_PIN_FUNC);
        qosa_pin_set_func((qosa_pin_num_e)PIN_PWM_R, PWM_PIN_FUNC);
    }
#endif
#if MOTOR_SELFTEST
    QLOGI("[balancing_car] motor self-test start");
    /* one wheel at a time, 1.5 s each, 3 s stop in between. order:
       1 left 20%  2 right 20%  3 left 70%  4 right 70%  5 left reverse 50%  6 right reverse 50%
       7 BOTH 20%  8 BOTH 70%  (separate PWM channels now: both wheels must get faster together)
       after every step the four encoder counters are logged (LA LB = left A/B, RA RB = right A/B) */
    {
        static const int idx_tab[8] = { 0, 1, 0, 1, 0, 1, 2, 2 };
        static const int val_tab[8] = { MOTOR_LIMIT / 5, MOTOR_LIMIT / 5, MOTOR_LIMIT * 7 / 10, MOTOR_LIMIT * 7 / 10, -MOTOR_LIMIT / 2, -MOTOR_LIMIT / 2, MOTOR_LIMIT / 5, MOTOR_LIMIT * 7 / 10 };
        qosa_task_sleep_ms(3000);
        for (int i = 0; i < 8; i++)
        {
            int32_t b_la = g_cnt_la, b_lb = g_cnt_lb, b_ra = g_cnt_ra, b_rb = g_cnt_rb;
            QLOGI("[balancing_car] self-test step %d: %s value=%d", i + 1, idx_tab[i] == 2 ? "both" : (idx_tab[i] ? "right" : "left"), val_tab[i]);
            if (idx_tab[i] == 2)
                motor_set(val_tab[i], val_tab[i]);
            else
                motor_set_one(idx_tab[i], val_tab[i] * (idx_tab[i] ? MOTOR_R_SIGN : MOTOR_L_SIGN));
            qosa_task_sleep_ms(1500);
            motor_stop();
            QLOGI("[balancing_car] step %d encoder delta: LA=%d LB=%d RA=%d RB=%d", i + 1,
                  (int)(g_cnt_la - b_la), (int)(g_cnt_lb - b_lb), (int)(g_cnt_ra - b_ra), (int)(g_cnt_rb - b_rb));
            qosa_task_sleep_ms(3000);
        }
    }
    QLOGI("[balancing_car] motor self-test done, encL=%d encR=%d",
          (int)(g_cnt_la + g_cnt_lb), (int)(g_cnt_ra + g_cnt_rb));
#endif
#if ENC_HAND_TEST
    /* automatic encoder test (no manual action needed, keep the wheels off the ground).
       The motors spin the wheels; every pin is polled every ~2 ms.
       tg = level changes seen by polling, cnt = interrupt counts during the step, lv = final levels (LA LB RA RB).
       steps: 0 idle, 1 left fwd, 2 right fwd, 3 left rev, 4 right rev, 5 idle, 6 both fwd */
    QLOGI("[balancing_car] ENC auto test start");
    /* pull test: if the pin follows the internal pull (down=0, up=1) it is floating (nothing drives it);
       if it reads the same level with both pulls an external device/board drives it */
    {
        static const char *const names[4] = { "LA PIN28", "LB PIN29", "RA PIN23", "RB PIN66" };
        for (int i = 0; i < 4; i++)
        {
            int dn, up, none;
            if (g_gpio_enc[i] < 0)
                continue;
            qosa_gpio_set_pull((qosa_gpio_num_e)g_gpio_enc[i], QOSA_GPIO_PULL_DOWN);
            qosa_task_sleep_ms(50);
            dn = gpio_in(g_gpio_enc[i]);
            qosa_gpio_set_pull((qosa_gpio_num_e)g_gpio_enc[i], QOSA_GPIO_PULL_UP);
            qosa_task_sleep_ms(50);
            up = gpio_in(g_gpio_enc[i]);
            qosa_gpio_set_pull((qosa_gpio_num_e)g_gpio_enc[i], QOSA_GPIO_PULL_NONE);
            qosa_task_sleep_ms(50);
            none = gpio_in(g_gpio_enc[i]);
            qosa_gpio_set_pull((qosa_gpio_num_e)g_gpio_enc[i], QOSA_GPIO_PULL_UP);
            QLOGI("[balancing_car] PULL TEST %s: pull-down=%d pull-up=%d no-pull=%d", names[i], dn, up, none);
        }
    }
    qosa_task_sleep_ms(2000);
    {
        static const int idx_tab[7] = { -1, 0, 1, 0, 1, -1, 2 };
        static const int val_tab[7] = { 0, MOTOR_LIMIT * 4 / 10, MOTOR_LIMIT * 4 / 10, -MOTOR_LIMIT * 4 / 10, -MOTOR_LIMIT * 4 / 10, 0, MOTOR_LIMIT * 4 / 10 };
        for (int s = 0; s < 7; s++)
        {
            int last_lv[4], tg[4];
            int32_t b_la = g_cnt_la, b_lb = g_cnt_lb, b_ra = g_cnt_ra, b_rb = g_cnt_rb;
            for (int i = 0; i < 4; i++)
            {
                last_lv[i] = g_gpio_enc[i] >= 0 ? gpio_in(g_gpio_enc[i]) : 0;
                tg[i] = 0;
            }
            if (idx_tab[s] == 2)
                motor_set(val_tab[s], val_tab[s]);
            else if (idx_tab[s] >= 0)
                motor_set_one(idx_tab[s], val_tab[s] * (idx_tab[s] ? MOTOR_R_SIGN : MOTOR_L_SIGN));
            for (int k = 0; k < 1000; k++) /* ~2 s */
            {
                qosa_task_sleep_ms(2);
                for (int i = 0; i < 4; i++)
                {
                    int lv = g_gpio_enc[i] >= 0 ? gpio_in(g_gpio_enc[i]) : 0;
                    if (lv != last_lv[i]) { tg[i]++; last_lv[i] = lv; }
                }
            }
            motor_stop();
            QLOGI("[balancing_car] ENC step %d (%s) | poll tg LA=%d LB=%d RA=%d RB=%d | cnt LA=%d LB=%d RA=%d RB=%d | lv %d%d%d%d", s,
                  idx_tab[s] < 0 ? "idle" : (idx_tab[s] == 2 ? "both" : (idx_tab[s] ? "right" : "left")),
                  tg[0], tg[1], tg[2], tg[3],
                  (int)(g_cnt_la - b_la), (int)(g_cnt_lb - b_lb), (int)(g_cnt_ra - b_ra), (int)(g_cnt_rb - b_rb),
                  last_lv[0], last_lv[1], last_lv[2], last_lv[3]);
            qosa_task_sleep_ms(1500);
        }
    }
    QLOGI("[balancing_car] ENC auto test done");
#endif
    if (mpu_init() != 0)
    {
        QLOGE("[balancing_car] MPU6050 init failed, task exit");
        return;
    }
    last_la = g_cnt_la; last_lb = g_cnt_lb; last_ra = g_cnt_ra; last_rb = g_cnt_rb;

    while (1)
    {
        float angle, gyro_bal, accel_angle, dt;
        int   enc_l, enc_r, bal, vel, ml, mr;
        int32_t cla, clb, cra, crb;

        /* ---- 5 ms tick: MPU data-ready INT, fall back to a plain 5 ms sleep if INT is not seen ---- */
        if (use_int)
        {
            if (qosa_sem_wait(g_int_sem, 20) != QOSA_ERROR_OK)
            {
                if (++no_int_cycles >= 50)
                {
                    QLOGW("[balancing_car] no MPU INT on PIN%d, use 5ms timer instead", PIN_MPU_INT);
                    use_int = 0;
                }
            }
            else
            {
                no_int_cycles = 0;
            }
        }
        else
        {
            qosa_task_sleep_ms(5);
        }
        dt = 0.005f;

        if (mpu_read_raw(&ax, &ay, &az, &gx, &gz) != 0)
        {
            motor_stop();
            stopped = 1;
            if (++fail_cnt % 200 == 1)
                QLOGE("[balancing_car] MPU read fail");
            qosa_task_sleep_ms(5);
            continue;
        }
        fail_cnt = 0;

        /* ---- attitude (same convention as Get_Angle, Way_Angle = 2) ---- */
        accel_angle = atan2_deg((float)ay, (float)az);
        angle       = -kalman_x(accel_angle, (float)gx / 16.4f, dt);
        gyro_bal    = -(float)gx; /* raw LSB, same as STM32 Gyro_Balance */

        /* ---- encoders: counts in this 5 ms ---- */
        cla = g_cnt_la; clb = g_cnt_lb; cra = g_cnt_ra; crb = g_cnt_rb;
        enc_l = ENC_L_SIGN * ((cla - last_la) + (clb - last_lb));
        enc_r = ENC_R_SIGN * ((cra - last_ra) + (crb - last_rb));
        last_la = cla; last_lb = clb; last_ra = cra; last_rb = crb;

        bal = balance_ctrl(angle, gyro_bal);
        vel = velocity_ctrl(enc_l, enc_r, stopped);
        ml  = limit(bal + vel, -MOTOR_LIMIT, MOTOR_LIMIT);
        mr  = limit(bal + vel, -MOTOR_LIMIT, MOTOR_LIMIT);

        /* ---- state machine: stopped -> (held upright ~1 s) -> running -> (tilt > 40 deg) -> stopped ---- */
        if (f_abs(angle - MIDDLE_ANGLE) > ANGLE_CUTOFF)
        {
            stopped  = 1;
            hold_cnt = 0;
        }
        else if (stopped)
        {
            if (f_abs(angle - MIDDLE_ANGLE) < ANGLE_START)
            {
                if (++hold_cnt >= START_HOLD_CNT)
                {
                    stopped  = 0;
                    hold_cnt = 0;
                    QLOGI("[balancing_car] car released, balancing");
                }
            }
            else
            {
                hold_cnt = 0;
            }
        }

        if (stopped)
            motor_stop();
        else
            motor_set(ml, mr);

        if (++log_cnt >= 200)
        {
            log_cnt = 0;
            QLOGI("[balancing_car] angle*10=%d gyro=%d encL=%d encR=%d motor=%d stop=%d int=%u",
                  (int)(angle * 10), (int)gyro_bal, enc_l, enc_r, ml, stopped, (unsigned)(g_int_cnt - last_int));
            if (stopped)
                QLOGI("[balancing_car] waiting: hold car with angle*10 in [%d, %d] for 1s (hold=%d/%d)",
                      (int)((MIDDLE_ANGLE - ANGLE_START) * 10), (int)((MIDDLE_ANGLE + ANGLE_START) * 10), hold_cnt, START_HOLD_CNT);
            last_int = g_int_cnt;
        }
    }
}

/*===========================================================================
 *  App entry
 ===========================================================================*/
void balancing_car_init(void)
{
    QLOGI("[balancing_car] enter balancing car app");
    if (g_car_task == QOSA_NULL)
    {
        qosa_task_create(&g_car_task, CTRL_TASK_STACK, CTRL_TASK_PRIO, "balancing_car", balancing_car_task, QOSA_NULL, 1);
    }
}
UNIRTOS_APP_EXPORT(710, "balancing_car", balancing_car_init);
